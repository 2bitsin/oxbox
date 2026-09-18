// The failure paths only provokable through a posix call: the assertion below
// leans on msync's alignment rule. The win32 road has the same contract.

#include "oxbox/platform/file-writer.hpp"
#include "oxbox/platform/mapped-file.hpp"

#include "fixtures.hpp"
#include "privilege.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

using namespace oxbox;

namespace native = oxbox::platform::detail::native_file;
using namespace std::string_view_literals;

using platform::Backing;
using platform::FileWriter;
using platform::MapIntent;
using platform::MappedFile;
using platform::test::ReasonOf;
using platform::test::RestrictedDirectory;
using platform::test::TempPath;
using platform::test::WriteSample;
using utilities::AsBytes;
using utilities::WritableBytes;

namespace
{
  // A hole is fewer blocks than the file claims bytes, and st_blocks is the only
  // place to see it; it counts 512-byte units whatever the block size is.
  auto StoredBytes(std::filesystem::path const& path) -> std::uintmax_t
  {
    struct ::stat status{ };
    if (::stat(path.c_str(), &status) != 0) return 0u;
    return static_cast<std::uintmax_t>(status.st_blocks) * 512u;
  }
}

TEST(MappedFile, FlushingAMappingTheKernelRejectsIsReportedNotSwallowed)
{
  // msync requires a page-aligned address, so a span starting mid-page is the
  // reachable stand-in for a kernel that refused the flush.
  TempPath sample;
  WriteSample(sample.Path(), "0123456789abcdef"sv);
  MappedFile file{ sample.Path(), MapIntent::READ_WRITE };
  ASSERT_GE(file.size(), 16u);

  WritableBytes const unaligned{ file.data() + 1u, file.size() - 1u };
  EXPECT_THROW(platform::FlushMapping(unaligned), std::runtime_error);
}

// In a directory the caller may not write, the O_EXCL create says EACCES and the
// plain open of a file that is not there says ENOENT. The expected text is asked
// of the same library that produced it, so nothing depends on a locale.
TEST(MappedFile, ACreateRefusedForPermissionSaysSoRatherThanMissing)
{
  TempPath sample;
  auto const& directory{ sample.Directory() };
  auto const inside{ directory / "new.img" };

  // the ENOENT shape, for comparison: the directory itself is not there
  std::string absent;
  try { MappedFile const file{ sample.Path() / "absent" / "new.img", 4096u };
        FAIL() << "expected a throw"; }
  catch (std::runtime_error const& error) { absent = error.what(); }

  std::string refusal;
  std::string refused;
  {
    // read and search, no write: a create in here is EACCES for anyone
    RestrictedDirectory const shut{ directory, std::filesystem::perms{ 0555 } };
    refusal = shut.Refusal();
    if (refusal.empty())
      try { MappedFile const file{ inside, 4096u }; }
      catch (std::runtime_error const& error) { refused = error.what(); }
  }

  if (!refusal.empty())
    GTEST_SKIP() << "cannot make a directory this process may not write: "
                 << refusal;
  ASSERT_FALSE(refused.empty()) << "the create was expected to fail";
  EXPECT_EQ(ReasonOf(refused), std::system_category().message(EACCES))
    << refused;
  EXPECT_NE(ReasonOf(refused), ReasonOf(absent)) << refused << "\n" << absent;
}

// ftruncate stores no zeros, so HOST_DEFAULT already produces what SPARSE asks
// for. It measures against a control because ext4 and XFS allocate late and ZFS
// and btrfs compress zeros: only incompressible synced bytes say what is stored.
TEST(MappedFile, ASizedFileHoldsNoBlocksAndAskingForSparseKeepsItThatWay)
{
  constexpr std::size_t LARGE{ std::size_t{ 32u } << 20u };   // 32 MiB
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const control   { directory / "written.img" };
  auto const by_default{ directory / "default.img" };
  auto const by_request{ directory / "sparse.img"  };

  // a file that is definitely stored: 32 MiB nothing can compress
  {
    std::vector<std::byte> noise(LARGE);
    std::uint64_t state{ 0x9E3779B97F4A7C15u };
    for (auto& octet : noise)
    {
      state ^= state << 13u; state ^= state >> 7u; state ^= state << 17u;
      octet = static_cast<std::byte>(state & 0xFFu);
    }
    FileWriter writer{ control };
    writer.Write(AsBytes(noise));
    writer.Commit();
  }
  auto const stored{ StoredBytes(control) };
  if (stored < LARGE / 2u)
    GTEST_SKIP() << "this filesystem reports " << stored << " bytes stored "
                    "for " << LARGE << " of incompressible data, so its "
                    "block counts cannot answer what a hole costs";

  { MappedFile const host{ by_default, LARGE }; }
  { MappedFile const asked{ by_request, LARGE, Backing::SPARSE }; }

  ASSERT_EQ(std::filesystem::file_size(by_default), LARGE);
  ASSERT_EQ(std::filesystem::file_size(by_request), LARGE);

  // a length that was never written to is not stored, asked for or not
  EXPECT_LT(StoredBytes(by_default), stored / 2u)
    << "a file that was never written to is holding blocks";
  EXPECT_LT(StoredBytes(by_request), stored / 2u)
    << "asking for sparse produced a file that stores its zeros";
}

// This road grants sparseness unasked, so a SPARSE request is satisfied and says
// SPARSE: HOST_DEFAULT would have consumers warn about a cost never paid here.
TEST(MappedFile, ASparseRequestIsSatisfiedByThisHostAndSaysSoRatherThanNothing)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };

  MappedFile const asked{ directory / "sparse.img", 4096u, Backing::SPARSE };
  EXPECT_EQ(asked.BackingInForce(), Backing::SPARSE)
    << "a request this host grants for free was reported as refused";

  MappedFile const empty{ directory / "empty.img", 0u, Backing::SPARSE };
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.BackingInForce(), Backing::SPARSE);

  auto const file{ native::CreateExclusiveForWrite(directory / "raw.img",
                                                   native::NO_MODE) };
  EXPECT_EQ(native::ApplyBacking(file, Backing::SPARSE,
                                   directory / "raw.img"),
            Backing::SPARSE);
  EXPECT_EQ(native::ApplyBacking(file, Backing::HOST_DEFAULT,
                                   directory / "raw.img"),
            Backing::HOST_DEFAULT);
  native::CloseFile(file);
}
