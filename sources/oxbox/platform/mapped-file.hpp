#pragma once
// The mapping road. On win32 a live view keeps the section, and the section
// keeps the file object, so a replacing rename onto a mapped file fails: a
// MappedFile must be dropped before FileWriter::Commit. Posix does not care.

#include "oxbox/platform/native-file.hpp"

#include "oxbox/utilities/short-types.hpp"
#include "oxbox/utilities/span.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <utility>

namespace oxbox::platform::detail::mapped_file
{
  using namespace utilities;
  using namespace native_file;

  // private copies never reach the file; shared writes persist on unmap
  enum class MapIntent : U08 { READ_ONLY, READ_WRITE, COPY_ON_WRITE };

  // `file` is NO_FILE on posix, where msync(MS_SYNC) commits through the pages.
  struct Mapping
  {
    WritableBytes bytes  { };
    NativeFile    file   { NO_FILE };
    Backing       backing{ Backing::HOST_DEFAULT };   // what is in force
  };

  // An empty file maps to an empty span; writing a READ_ONLY mapping traps.
  auto MapFile(std::filesystem::path const& path, MapIntent intent) -> Mapping;
  auto UnmapFile(Mapping mapping) noexcept -> void;

  // Not a durability guarantee on win32 by itself; MappedFile::Flush is.
  auto FlushMapping(WritableBytes pages) -> void;

  // A `size` of 0 is an empty file and an empty mapping; a failure removes only
  // a file this call created, never one that was already there.
  auto MapFileForWrite(std::filesystem::path const& path, std::size_t size,
                       Backing backing = Backing::HOST_DEFAULT) -> Mapping;

  class MappedFile
  {
  public:
    MappedFile() noexcept = default;

    MappedFile(std::filesystem::path const& path, MapIntent intent)
    : _mapping{ MapFile(path, intent) }
    { }

    MappedFile(std::filesystem::path const& path, std::size_t size,
               Backing backing = Backing::HOST_DEFAULT)
    : _mapping{ MapFileForWrite(path, size, backing) }
    { }

    MappedFile(MappedFile const&)                    = delete;
    auto operator = (MappedFile const&) -> MappedFile& = delete;

    MappedFile(MappedFile&& other) noexcept
    : _mapping{ std::exchange(other._mapping, { }) }
    { }

    auto operator = (MappedFile&& other) noexcept -> MappedFile&
    { std::swap(_mapping, other._mapping); return *this; }

    ~MappedFile() noexcept
    { UnmapFile(_mapping); }

    // HOST_DEFAULT after a SPARSE request means the length costs real disk.
    auto BackingInForce() const noexcept -> Backing { return _mapping.backing; }

    auto size () const noexcept -> std::size_t     { return _mapping.bytes.size();  }
    auto empty() const noexcept -> bool            { return _mapping.bytes.empty(); }
    auto data ()       noexcept -> std::byte*       { return _mapping.bytes.data(); }
    auto data () const noexcept -> std::byte const* { return _mapping.bytes.data(); }

    auto begin()       noexcept { return _mapping.bytes.begin(); }
    auto end  ()       noexcept { return _mapping.bytes.end();   }
    auto begin() const noexcept { return Bytes{ _mapping.bytes }.begin(); }
    auto end  () const noexcept { return Bytes{ _mapping.bytes }.end();   }

    auto operator [] (std::size_t index)       noexcept -> std::byte&       { return _mapping.bytes[index]; }
    auto operator [] (std::size_t index) const noexcept -> std::byte const& { return _mapping.bytes[index]; }

    operator WritableBytes ()       noexcept { return _mapping.bytes; }
    operator Bytes         () const noexcept { return _mapping.bytes; }

    // msync(MS_SYNC) is durable on posix; win32 needs FlushFileBuffers too,
    // since FlushViewOfFile only reaches the system cache.
    auto Flush() -> void
    { if (!_mapping.bytes.empty()) FlushMapping(_mapping.bytes);
      SyncFile(_mapping.file); }

  private:
    Mapping _mapping{ };
  };
}

namespace oxbox::platform
{
  using detail::native_file::Backing;

  using detail::mapped_file::FlushMapping;
  using detail::mapped_file::MapFile;
  using detail::mapped_file::MapFileForWrite;
  using detail::mapped_file::MapIntent;
  using detail::mapped_file::MappedFile;
  using detail::mapped_file::Mapping;
  using detail::mapped_file::UnmapFile;
}
