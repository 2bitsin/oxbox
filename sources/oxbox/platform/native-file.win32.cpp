// The OS handle's verbs, win32.

#include "oxbox/platform/native-file.hpp"

#include "oxbox/platform/native-file.win32.hpp"
#include "oxbox/utilities/path.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace oxbox::platform::detail::native_file
{
  auto Failure(std::string_view what,
               std::filesystem::path const& path) -> std::string
  {
    auto const code{ static_cast<int>(::GetLastError()) };
    return std::format("platform: {} '{}': {}",
                       what, PathToString(path),
                       std::system_category().message(code));
  }

  auto Failure(std::string_view what, std::filesystem::path const& path,
               std::size_t bytes) -> std::string
  {
    auto const code{ static_cast<int>(::GetLastError()) };
    return std::format("platform: {} '{}' ({:#x} bytes): {}",
                       what, PathToString(path), bytes,
                       std::system_category().message(code));
  }

  auto Failure(std::string_view what) -> std::string
  {
    auto const code{ static_cast<int>(::GetLastError()) };
    return std::format("platform: {}: {}",
                       what, std::system_category().message(code));
  }

  auto ModeOf(std::filesystem::path const& path) -> FileMode
  {
    auto const attributes{ ::GetFileAttributesW(path.c_str()) };
    return attributes == INVALID_FILE_ATTRIBUTES
         ? NO_MODE : static_cast<FileMode>(attributes & CARRIED_ATTRIBUTES);
  }


  auto OpenForWrite(std::filesystem::path const& path) -> OpenedFile
  {
    // CREATE_ALWAYS creates or truncates; the last error says which.
    auto const handle{ AsNative(::CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr)) };
    auto const created{ ::GetLastError() != ERROR_ALREADY_EXISTS };
    if (handle == NO_FILE)
      throw std::runtime_error{ Failure("cannot create", path) };
    return { handle, created };
  }


  auto CreateExclusiveForWrite(std::filesystem::path const& path,
                               FileMode mode) -> NativeFile
  {
    // Win32 attributes are not permissions -- ACLs are, and a new file inherits
    // the directory's -- so ApplyMode can wait until commit.
    static_cast<void>(mode);
    auto const handle{ AsNative(::CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr,
      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)) };
    if (handle == NO_FILE)
      throw std::runtime_error{ Failure("cannot create", path) };
    return handle;
  }


  auto ApplyBacking(NativeFile file, Backing backing,
                    std::filesystem::path const& path) -> Backing
  {
    if (backing != Backing::SPARSE) return Backing::HOST_DEFAULT;

    // SetEndOfFile leaves the valid data length behind, and the first write past
    // it zeroes everything in between; the mark must precede the size to count.
    ::FILE_SET_SPARSE_BUFFER request{ };
    request.SetSparse = TRUE;
    ::DWORD returned{ 0u };
    if (::DeviceIoControl(Handle(file), FSCTL_SET_SPARSE,
                          &request, sizeof(request),
                          nullptr, 0u, &returned, nullptr) != 0)
      return Backing::SPARSE;

    // A volume with no sparse files in it -- FAT32, exFAT -- says so, and the
    // file is made anyway; the return value is how the caller learns the cost.
    auto const code{ ::GetLastError() };
    if (code == ERROR_INVALID_FUNCTION || code == ERROR_NOT_SUPPORTED)
      return Backing::HOST_DEFAULT;
    throw std::runtime_error{ Failure("cannot make sparse", path) };
  }

  auto ApplyMode(NativeFile file, FileMode mode,
                 std::filesystem::path const& path) -> void
  {
    if (mode == NO_MODE) return;          // a new file keeps the host default
    // through the handle and not the name, which is about to stop existing
    ::FILE_BASIC_INFO information{ };
    // Zero means "leave them alone" and not "clear them", so a carried set that
    // masked down to nothing has to be spelled as FILE_ATTRIBUTE_NORMAL.
    auto const carried{ static_cast<::DWORD>(mode) };
    information.FileAttributes = carried == 0u ? DWORD{ FILE_ATTRIBUTE_NORMAL }
                                               : carried;
    if (::SetFileInformationByHandle(Handle(file), FileBasicInfo,
                                     &information,
                                     sizeof(information)) == 0)
      throw std::runtime_error{ Failure("cannot set the attributes of",
                                        path) };
  }


  auto WriteToFile(NativeFile file, Bytes bytes,
                   std::filesystem::path const& path) -> void
  {
    auto remaining{ bytes };
    while (!remaining.empty())
    {
      auto const chunk{ static_cast<::DWORD>(
        std::min<std::size_t>(remaining.size(), MAX_WRITE_CHUNK)) };
      ::DWORD written{ 0u };
      // zero written of a non-empty buffer is no progress, and the loop spins
      if (::WriteFile(Handle(file), remaining.data(), chunk, &written,
                      nullptr) == 0 || written == 0u)
        throw std::runtime_error{ Failure("write to", path) };
      remaining = remaining.subspan(written);
    }
  }


  auto CloseFile(NativeFile file) noexcept -> void
  {
    if (file != NO_FILE) ::CloseHandle(Handle(file));
  }


  auto RenameOver(std::filesystem::path const& from,
                  std::filesystem::path const& to) -> void
  {
    // MOVEFILE_REPLACE_EXISTING, unlike ReplaceFileW, also works when the target
    // does not exist yet; WRITE_THROUGH returns only once the rename is on disk.
    // A READONLY target refuses it outright with ACCESS_DENIED.
    if (::MoveFileExW(from.c_str(), to.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
      auto const code{ static_cast<int>(::GetLastError()) };
      throw std::runtime_error{ std::format(
        "platform: cannot rename '{}' onto '{}': {}",
        PathToString(from), PathToString(to),
        std::system_category().message(code)) };
    }
  }

  auto SyncFile(NativeFile file) -> void
  {
    // The half FlushViewOfFile does not do: without it a shared-write mapping
    // reports writes as committed while they sit in the cache.
    if (file == NO_FILE) return;
    if (::FlushFileBuffers(Handle(file)) == 0)
      throw std::runtime_error{ Failure("FlushFileBuffers failed") };
  }


  auto SyncDirectoryOf(std::filesystem::path const& path) -> void
  {
    // Nothing to do: MOVEFILE_WRITE_THROUGH already waited for the entry.
    static_cast<void>(path);
  }

}
