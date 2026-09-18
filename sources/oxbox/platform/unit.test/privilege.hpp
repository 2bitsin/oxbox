#pragma once
// A directory mode does not restrict root, so it comes with a matching drop
// of the effective uid. Posix-only: headers carry no platform tag.

#include <cerrno>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace oxbox::platform::test
{
  // 65534 is only the conventional answer; a container image may disagree.
  inline auto NobodyUid() -> ::uid_t
  {
    constexpr ::uid_t CONVENTIONAL_NOBODY{ 65534u };
    auto const* const entry{ ::getpwnam("nobody") };
    return entry == nullptr ? CONVENTIONAL_NOBODY : entry->pw_uid;
  }

  class RestrictedDirectory
  {
  public:
    RestrictedDirectory(std::filesystem::path const& path,
                        std::filesystem::perms       mode)
    : _path    { path }
    , _restored{ std::filesystem::status(path).permissions() }
    {
      std::filesystem::permissions(_path, mode);   // throws if it cannot
      if (::geteuid() != 0u) return;               // already bound by the mode
      if (::seteuid(NobodyUid()) != 0)
      {
        // root without CAP_SETUID, or a user namespace with no `nobody`
        // mapping: the restriction cannot be made real, so the test skips
        // rather than pass while proving nothing.
        _refusal = "seteuid(nobody): "
                 + std::system_category().message(errno);
        return;
      }
      _dropped = true;
    }

    RestrictedDirectory(RestrictedDirectory const&)                    = delete;
    auto operator = (RestrictedDirectory const&) -> RestrictedDirectory& = delete;
    RestrictedDirectory(RestrictedDirectory&&)                         = delete;
    auto operator = (RestrictedDirectory&&) -> RestrictedDirectory&      = delete;

    ~RestrictedDirectory() noexcept
    {
      if (_dropped) static_cast<void>(::seteuid(0u));
      std::error_code ignored;
      std::filesystem::permissions(_path, _restored, ignored);
    }

    // empty when the restriction is in force; a reason to skip when it is not
    auto Refusal() const noexcept -> std::string_view { return _refusal; }

  private:
    std::filesystem::path  _path;
    std::filesystem::perms _restored;
    bool                   _dropped{ false };
    std::string            _refusal{ };
  };
}
