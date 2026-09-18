#pragma once
// A directory nothing else is using: created under a per-process token and
// a per-area serial, and removed recursively when it goes out of scope.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace oxbox::platform::detail::scratch_area
{
  inline constexpr std::string_view SCRATCH_PROGRAM{ "oxbox" };

  // `<temp>/<program>-<token>-<serial>-<purpose>`, so `purpose` has to be a
  // filename. A path that already exists throws like any other failure.
  [[nodiscard]] auto ScratchDirectory(std::string_view purpose,
                                      std::string_view program = SCRATCH_PROGRAM)
    -> std::filesystem::path;

  // Move-only: two owners of one directory is one owner too many.
  class ScratchArea
  {
  public:
    explicit ScratchArea(std::string_view purpose,
                         std::string_view program = SCRATCH_PROGRAM)
    : _path{ ScratchDirectory(purpose, program) }
    { }

    ScratchArea(ScratchArea const&)                      = delete;
    auto operator = (ScratchArea const&) -> ScratchArea& = delete;

    ScratchArea(ScratchArea&& other) noexcept
    : _path{ std::exchange(other._path, { }) }
    { }

    auto operator = (ScratchArea&& other) noexcept -> ScratchArea&
    {
      if (this != &other)
      {
        Remove();
        _path = std::exchange(other._path, { });
      }
      return *this;
    }

    ~ScratchArea() noexcept { Remove(); }

    [[nodiscard]] auto Path() const noexcept -> std::filesystem::path const&
    { return _path; }

    // The path only; nothing is created here.
    [[nodiscard]] auto File(std::string_view name) const -> std::filesystem::path
    { return _path / name; }

    operator std::filesystem::path const& () const noexcept { return _path; }

  private:
    // Never throws: a destructor's failure has nobody to tell.
    auto Remove() noexcept -> void;

    std::filesystem::path _path;
  };

  [[nodiscard]] auto ScratchToken() noexcept -> std::uint64_t;
}

namespace oxbox::platform
{
  using detail::scratch_area::SCRATCH_PROGRAM;
  using detail::scratch_area::ScratchArea;
  using detail::scratch_area::ScratchDirectory;
  using detail::scratch_area::ScratchToken;
}
