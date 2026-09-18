// The mapping road, win32.

#include "oxbox/platform/mapped-file.hpp"

#include "oxbox/platform/native-file.win32.hpp"
#include "oxbox/utilities/path.hpp"

#include <format>
#include <stdexcept>

namespace oxbox::platform::detail::mapped_file
{
  namespace
  {
    auto Protection(MapIntent intent) -> ::DWORD
    {
      switch (intent)
      {
        case MapIntent::READ_ONLY:     return PAGE_READONLY;
        case MapIntent::READ_WRITE:    return PAGE_READWRITE;
        case MapIntent::COPY_ON_WRITE: return PAGE_WRITECOPY;
      }
      // exhaustive over the enumeration: reachable only from a cast value
      throw std::runtime_error{ "platform: unhandled intent" };  // GCOVR_EXCL_LINE
    }

    auto ViewAccess(MapIntent intent) -> ::DWORD
    {
      switch (intent)
      {
        case MapIntent::READ_ONLY:     return FILE_MAP_READ;
        case MapIntent::READ_WRITE:    return FILE_MAP_READ | FILE_MAP_WRITE;
        case MapIntent::COPY_ON_WRITE: return FILE_MAP_COPY;
      }
      throw std::runtime_error{ "platform: unhandled intent" };  // GCOVR_EXCL_LINE
    }
  }

  auto MapFile(std::filesystem::path const& path, MapIntent intent) -> Mapping
  {
    auto const wants_write{ intent == MapIntent::READ_WRITE };
    ScopedFile file{ AsNative(::CreateFileW(
      path.c_str(),
      wants_write ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
      nullptr)) };
    if (file.Get() == NO_FILE)
      throw std::runtime_error{ Failure("cannot open", path) };

    ::LARGE_INTEGER measured{ };
    if (::GetFileSizeEx(Handle(file.Get()), &measured) == 0)
      throw std::runtime_error{ Failure("cannot size", path) };
    auto const bytes{ static_cast<std::size_t>(measured.QuadPart) };
    if (bytes == 0u) return { };

    ScopedFile const section{ AsNative(::CreateFileMappingW(
      Handle(file.Get()), nullptr, Protection(intent), 0u, 0u, nullptr)) };
    if (section.Get() == NO_FILE)
      throw std::runtime_error{ Failure("cannot create mapping for", path) };

    auto const view{ ::MapViewOfFile(Handle(section.Get()),
                                     ViewAccess(intent), 0u, 0u, 0u) };
    if (view == nullptr)
      throw std::runtime_error{ Failure("cannot map view of", path, bytes) };

    // The view keeps the section and the section keeps the file; the handle is
    // held only for a shared write, whose Flush needs FlushFileBuffers -- and
    // it must be held, because a handle cannot be recovered from a view.
    return { WritableBytes{ static_cast<std::byte*>(view), bytes },
             wants_write ? file.Release() : NO_FILE };
  }

  auto UnmapFile(Mapping mapping) noexcept -> void
  {
    if (!mapping.bytes.empty())
      ::UnmapViewOfFile(mapping.bytes.data());
    if (mapping.file != NO_FILE)
      CloseFile(mapping.file);
  }

  auto FlushMapping(WritableBytes pages) -> void
  {
    // This reaches the system cache and no further; MappedFile::Flush is both.
    if (pages.empty()) return;
    if (::FlushViewOfFile(pages.data(), pages.size()) == 0)
      throw std::runtime_error{ Failure("FlushViewOfFile failed") };
  }


  auto MapFileForWrite(std::filesystem::path const& path, std::size_t size,
                       Backing backing) -> Mapping
  {
    // OPEN_ALWAYS says which of the two it did through the last error, which
    // decides whether the cleanup may delete the file; the next call resets it.
    ScopedFile file{ AsNative(::CreateFileW(
      path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) };
    auto const created{ ::GetLastError() != ERROR_ALREADY_EXISTS };
    if (file.Get() == NO_FILE)
      throw std::runtime_error{ Failure("cannot create", path) };
    NewFileGuard fresh{ path, created };

    auto const granted{ ApplyBacking(file.Get(), backing, path) };
    ::LARGE_INTEGER wanted{ };
    wanted.QuadPart = static_cast<::LONGLONG>(size);
    if (::SetFilePointerEx(Handle(file.Get()), wanted, nullptr, FILE_BEGIN) == 0
     || ::SetEndOfFile(Handle(file.Get())) == 0)
      throw std::runtime_error{ Failure("cannot size", path, size) };

    // an empty file is an empty mapping, and the file has still been created
    if (size == 0u)
    { fresh.Commit(); return { { }, NO_FILE, granted }; }

    ScopedFile const section{ AsNative(::CreateFileMappingW(
      Handle(file.Get()), nullptr, PAGE_READWRITE,
      static_cast<::DWORD>(wanted.QuadPart >> 32u),
      static_cast<::DWORD>(wanted.QuadPart), nullptr)) };
    if (section.Get() == NO_FILE)
      throw std::runtime_error{ Failure("cannot create mapping for", path,
                                        size) };

    auto const view{ ::MapViewOfFile(Handle(section.Get()),
                                     FILE_MAP_ALL_ACCESS, 0u, 0u, size) };
    if (view == nullptr)
      throw std::runtime_error{ Failure("cannot map view of", path, size) };

    fresh.Commit();
    // shared and writable, so the file is kept for Flush
    return { WritableBytes{ static_cast<std::byte*>(view), size },
             file.Release(), granted };
  }

}
