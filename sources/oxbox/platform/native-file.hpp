#pragma once
// The OS handle and the verbs this module speaks to one. Not re-exported into
// oxbox::platform, which is what keeps it private to the module.

#include "oxbox/utilities/short-types.hpp"
#include "oxbox/utilities/span.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace oxbox::platform::detail::native_file
{
  using namespace utilities;

  // A posix file given a length it was never written to is already sparse, so
  // SPARSE is a no-op there; NTFS is not, and it is a request, not a promise.
  enum class Backing : U08
  {
    HOST_DEFAULT,
    SPARSE
  };

  // NO_FILE is all-ones: -1 on posix, INVALID_HANDLE_VALUE on win32.
  using NativeFile = std::uintptr_t;
  inline constexpr NativeFile NO_FILE{ ~NativeFile{ 0u } };

  struct OpenedFile
  {
    NativeFile file   { NO_FILE };
    bool       created{ false };   // cleanup deletes only what we created
  };

  // Opaque: a mode_t on posix, an attribute word on win32; NO_MODE is none.
  using FileMode = std::uint32_t;
  inline constexpr FileMode NO_MODE{ ~FileMode{ 0u } };

  // stat and not lstat -- a symlink's own mode is meaningless.
  auto ModeOf(std::filesystem::path const& path) -> FileMode;

  auto OpenForWrite(std::filesystem::path const& path) -> OpenedFile;

  // Posix creates it with `mode`, so the write is never world-readable.
  auto CreateExclusiveForWrite(std::filesystem::path const& path,
                               FileMode mode) -> NativeFile;

  // FSCTL_SET_SPARSE must be issued between create and SetEndOfFile, or NTFS
  // writes real zeros up to the valid data length. Returns what is in force.
  auto ApplyBacking(NativeFile file, Backing backing,
                    std::filesystem::path const& path) -> Backing;

  // Creation narrows a mode by the umask and this does not.
  auto ApplyMode(NativeFile file, FileMode mode,
                 std::filesystem::path const& path) -> void;

  auto WriteToFile(NativeFile file, Bytes bytes,
                   std::filesystem::path const& path) -> void;
  auto CloseFile(NativeFile file) noexcept -> void;

  auto RenameOver(std::filesystem::path const& from,
                  std::filesystem::path const& to) -> void;

  auto SyncFile(NativeFile file) -> void;

  // Without it a file's bytes are durable and its name is not. Nothing to do
  // on win32, where MOVEFILE_WRITE_THROUGH already paid it.
  auto SyncDirectoryOf(std::filesystem::path const& path) -> void;

  class ScopedFile
  {
  public:
    explicit ScopedFile(NativeFile file) noexcept
    : _file{ file }
    { }

    ScopedFile(ScopedFile const&)                    = delete;
    auto operator = (ScopedFile const&) -> ScopedFile& = delete;
    ScopedFile(ScopedFile&&)                         = delete;
    auto operator = (ScopedFile&&) -> ScopedFile&      = delete;

    ~ScopedFile() noexcept
    { if (_file != NO_FILE) CloseFile(_file); }

    auto Get    () const noexcept -> NativeFile { return _file; }
    auto Release()       noexcept -> NativeFile { return std::exchange(_file, NO_FILE); }

  private:
    NativeFile _file{ NO_FILE };
  };

  class NewFileGuard
  {
  public:
    NewFileGuard(std::filesystem::path const& path, bool created)
    : _path{ created ? path : std::filesystem::path{ } }
    { }

    NewFileGuard(NewFileGuard const&)                    = delete;
    auto operator = (NewFileGuard const&) -> NewFileGuard& = delete;
    NewFileGuard(NewFileGuard&&)                         = delete;
    auto operator = (NewFileGuard&&) -> NewFileGuard&      = delete;

    ~NewFileGuard() noexcept
    { if (!_path.empty())
      { std::error_code ignored; std::filesystem::remove(_path, ignored); } }

    auto Commit() noexcept -> void { _path.clear(); }

  private:
    std::filesystem::path _path;
  };
}
