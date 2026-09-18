// The failure paths that need a Linux facility: setrlimit(RLIMIT_AS) is accepted
// on Darwin but not enforced in its mmap, so the mapping there would succeed.

#include "oxbox/platform/mapped-file.hpp"

#include "fixtures.hpp"
#include "oxbox/utilities/path.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>

#include <sys/resource.h>
#include <unistd.h>

using namespace oxbox;

using platform::MapIntent;
using platform::MappedFile;
using platform::test::Contains;
using platform::test::ReasonOf;
using platform::test::TempPath;
using utilities::PathToString;

namespace
{
  // large enough that no address space with the headroom below can hold it
  constexpr std::size_t OVERSIZED_FILE{ std::size_t{ 256u } << 20u };  // 256 MiB

  // generous on purpose: too tight and gtest's own allocations become bad_alloc
  constexpr std::size_t HEADROOM{ std::size_t{ 64u } << 20u };         // 64 MiB

  // A limit the kernel enforces on every mapping, rather than an oversized file:
  // ext4 caps a file at 16 TiB, which maps fine on a 128 TiB address space.
  class AddressSpaceLimit
  {
  public:
    explicit AddressSpaceLimit(std::size_t headroom)
    {
      // a squeeze that was refused would leave the mapping below succeeding
      if (::getrlimit(RLIMIT_AS, &_saved) != 0)
      { _refusal = "getrlimit(RLIMIT_AS)"; return; }
      auto tightened{ _saved };
      // InUse() reads /proc and allocates, so it runs before the squeeze
      tightened.rlim_cur = InUse() + static_cast<::rlim_t>(headroom);
      if (::setrlimit(RLIMIT_AS, &tightened) != 0)
      { _refusal = "setrlimit(RLIMIT_AS)"; return; }
      _squeezed = true;
    }

    // empty when the address space really is squeezed
    auto Refusal() const noexcept -> std::string_view { return _refusal; }

    AddressSpaceLimit(AddressSpaceLimit const&)                    = delete;
    auto operator = (AddressSpaceLimit const&) -> AddressSpaceLimit& = delete;
    AddressSpaceLimit(AddressSpaceLimit&&)                         = delete;
    auto operator = (AddressSpaceLimit&&) -> AddressSpaceLimit&      = delete;

    ~AddressSpaceLimit() noexcept
    { if (_squeezed) ::setrlimit(RLIMIT_AS, &_saved); }

  private:
    // statm's first field is VmSize in pages, what RLIMIT_AS is compared against
    static auto InUse() -> ::rlim_t
    {
      std::ifstream statm{ "/proc/self/statm" };
      std::size_t pages{ 0u };
      statm >> pages;
      return static_cast<::rlim_t>(pages)
           * static_cast<::rlim_t>(::sysconf(_SC_PAGESIZE));
    }

    ::rlimit         _saved  { };
    bool             _squeezed{ false };
    std::string_view _refusal { };
  };

  // everything that allocates for gtest happens outside the limit
  struct UnderPressure
  {
    std::string      reported;   // what the attempt threw, or empty
    std::string_view refusal;    // why there was no pressure, or empty
  };

  template <typename _Attempt>
  auto FailureUnderPressure(_Attempt attempt) -> UnderPressure
  {
    UnderPressure outcome;
    {
      AddressSpaceLimit const squeezed{ HEADROOM };
      outcome.refusal = squeezed.Refusal();
      if (outcome.refusal.empty())
        try { attempt(); } catch (std::runtime_error const& error)
        { outcome.reported = error.what(); }
    }
    return outcome;
  }
}

TEST(MappedFile, AMappingTooLargeForTheAddressSpaceFailsByName)
{
  TempPath sample;
  { std::ofstream out{ sample.Path(), std::ios::binary }; }
  std::error_code sizing;
  std::filesystem::resize_file(sample.Path(), OVERSIZED_FILE, sizing);
  ASSERT_FALSE(sizing) << "could not make a 256 MiB sparse file: "
                       << sizing.message();

  auto const [reported, refusal]{ FailureUnderPressure([&]
  { MappedFile const file{ sample.Path(), MapIntent::READ_ONLY }; }) };

  ASSERT_TRUE(refusal.empty()) << "the address space was not squeezed: "
                               << refusal << " was refused";
  ASSERT_FALSE(reported.empty()) << "the mapping was expected to fail";
  EXPECT_TRUE(Contains(reported, "mmap"))                          << reported;
  EXPECT_TRUE(Contains(reported, PathToString(sample.Path())))     << reported;
  EXPECT_FALSE(ReasonOf(reported).empty())                         << reported;
}

TEST(MappedFile, CreatingAMappingTooLargeToMapFailsByNameAndLeavesNoFile)
{
  TempPath sample;
  auto const path{ sample.Directory() / "huge.img" };

  auto const [reported, refusal]{ FailureUnderPressure([&]
  { MappedFile const file{ path, OVERSIZED_FILE }; }) };

  ASSERT_TRUE(refusal.empty()) << "the address space was not squeezed: "
                               << refusal << " was refused";
  ASSERT_FALSE(reported.empty()) << "the mapping was expected to fail";
  EXPECT_TRUE(Contains(reported, "mmap"))              << reported;
  EXPECT_TRUE(Contains(reported, PathToString(path)))  << reported;
  EXPECT_FALSE(std::filesystem::exists(path))
    << "a create that failed at the mapping left its file behind";
}
