// The mapping road, POSIX.

#include "oxbox/platform/mapped-file.hpp"

#include "oxbox/platform/native-file.posix.hpp"
#include "oxbox/utilities/path.hpp"

#include <cerrno>
#include <format>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace oxbox::platform::detail::mapped_file
{
  namespace
  {
    auto Protection(MapIntent intent) -> int
    {
      switch (intent)
      {
        case MapIntent::READ_ONLY:     return PROT_READ;
        case MapIntent::READ_WRITE:
        case MapIntent::COPY_ON_WRITE: return PROT_READ | PROT_WRITE;
      }
      // the switch is exhaustive, so this is reachable only from a cast value
      throw std::runtime_error{ "platform: unhandled intent" };  // GCOVR_EXCL_LINE
    }

    auto Visibility(MapIntent intent) -> int
    { return intent == MapIntent::READ_WRITE ? MAP_SHARED : MAP_PRIVATE; }

  }

  auto MapFile(std::filesystem::path const& path, MapIntent intent) -> Mapping
  {
    auto const wants_write{ intent == MapIntent::READ_WRITE };
    ScopedFile const file{ static_cast<NativeFile>(
      ::open(path.c_str(), wants_write ? O_RDWR : O_RDONLY)) };
    if (file.Get() == NO_FILE)
      throw std::runtime_error{ Failure("cannot open", path) };

    struct ::stat status{ };
    // GCOVR_EXCL_START -- fstat on a descriptor open() just handed back fails
    // only for EBADF (the descriptor is held) or EOVERFLOW (a size past
    // off_t, so a 32-bit build without large-file support).
    if (::fstat(Descriptor(file.Get()), &status) != 0)
      throw std::runtime_error{ Failure("cannot stat", path) };
    // GCOVR_EXCL_STOP
    auto const bytes{ static_cast<std::size_t>(status.st_size) };
    if (bytes == 0u) return { };

    auto const mapped{ ::mmap(nullptr, bytes, Protection(intent),
                              Visibility(intent), Descriptor(file.Get()), 0) };
    if (mapped == MAP_FAILED)
      throw std::runtime_error{ Failure("mmap failed for", path, bytes) };

    // the mapping keeps the file alive and msync commits through it
    return { WritableBytes{ static_cast<std::byte*>(mapped), bytes }, NO_FILE };
  }

  auto UnmapFile(Mapping mapping) noexcept -> void
  {
    if (!mapping.bytes.empty())
      ::munmap(mapping.bytes.data(), mapping.bytes.size());
    if (mapping.file != NO_FILE)
      CloseFile(mapping.file);      // never on this road; the shape is shared
  }

  auto FlushMapping(WritableBytes pages) -> void
  {
    if (pages.empty()) return;
    if (::msync(pages.data(), pages.size(), MS_SYNC) != 0)
      throw std::runtime_error{ Failure("msync failed") };
  }

  auto MapFileForWrite(std::filesystem::path const& path, std::size_t size,
                       Backing backing) -> Mapping
  {
    // O_EXCL first, so "this call created it" is an answer and not a guess: it is
    // what decides whether the cleanup below may delete the file.
    auto descriptor{ ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL,
                            NEW_FILE_MODE) };
    auto const created{ descriptor >= 0 };
    // Fall back only for EEXIST: in a directory the caller may not write, O_EXCL
    // says EACCES where a plain open of a missing file says ENOENT.
    if (!created && errno != EEXIST)
      throw std::runtime_error{ Failure("cannot create", path) };
    if (!created) descriptor = ::open(path.c_str(), O_RDWR);
    ScopedFile const file{ static_cast<NativeFile>(descriptor) };
    if (file.Get() == NO_FILE)
      // it exists, so this second attempt failed to open it, not to create it
      throw std::runtime_error{ Failure("cannot open", path) };
    NewFileGuard fresh{ path, created };

    auto const granted{ ApplyBacking(file.Get(), backing, path) };
    if (::ftruncate(Descriptor(file.Get()), static_cast<::off_t>(size)) != 0)
      throw std::runtime_error{ Failure("cannot size", path, size) };

    // an empty file is an empty mapping, and the file has still been created
    if (size == 0u)
    { fresh.Commit(); return { { }, NO_FILE, granted }; }

    auto const mapped{ ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, Descriptor(file.Get()), 0) };
    if (mapped == MAP_FAILED)
      throw std::runtime_error{ Failure("mmap failed for", path, size) };

    fresh.Commit();
    return { WritableBytes{ static_cast<std::byte*>(mapped), size }, NO_FILE,
             granted };
  }
}
