// The write road, composed out of native-file's substrate and no OS call.

#include "oxbox/platform/file-writer.hpp"

#include "oxbox/platform/native-file.hpp"
#include "oxbox/utilities/path.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <random>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace oxbox::platform::detail::file_writer
{
  namespace
  {
    // Beside the target: the rename that makes a save atomic is atomic only
    // within one filesystem.
    auto TemporaryBeside(std::filesystem::path const& target)
      -> std::filesystem::path
    {
      // one draw seeds a counter: the uniqueness that must hold is between
      // processes, not within one
      static std::atomic<std::uint64_t> sequence{ [ ] {
        std::random_device source;
        return (std::uint64_t{ source() } << 32u) | source();
      }() };

      auto temporary{ target };
      // the 21-character suffix is never shortened -- ENAMETOOLONG on a name
      // already near the host limit
      temporary += PathFromString(
        std::format(".{:016x}.tmp",
                    sequence.fetch_add(1u, std::memory_order_relaxed)));
      return temporary;
    }
  }

  auto WriteBinaryFile(std::filesystem::path const& path, Bytes bytes) -> void
  {
    auto const opened{ OpenForWrite(path) };
    ScopedFile   file { opened.file };
    NewFileGuard fresh{ path, opened.created };

    WriteToFile(file.Get(), bytes, path);
    fresh.Commit();
  }

  FileWriter::FileWriter(std::filesystem::path const& path)
  : _target   { path }
  , _temporary{ TemporaryBeside(path) }
  , _mode     { ModeOf(path) }         // asked before anything is created
  {
    // Born with the target's mode where the host can, so a replacement for a
    // 0600 file is never briefly world-readable. Exclusive: the name is ours.
    _file = CreateExclusiveForWrite(_temporary, _mode);
  }

  FileWriter::FileWriter(FileWriter&& other) noexcept
  : _target   { std::exchange(other._target,    { }) }
  , _temporary{ std::exchange(other._temporary, { }) }
  , _file     { std::exchange(other._file, NO_FILE)  }
  , _mode     { std::exchange(other._mode, NO_MODE)  }
  , _state    { std::exchange(other._state, State::SPENT) }
  , _failure  { std::exchange(other._failure,   { }) }
  { }

  // Discard then take, not a swap: Committed() and Path() are observable, and a
  // swap left the source reporting a commit it did not own.
  auto FileWriter::operator = (FileWriter&& other) noexcept -> FileWriter&
  {
    if (this == &other) return *this;

    Discard();                          // ours goes now, not eventually
    _target    = std::exchange(other._target,          { });
    _temporary = std::exchange(other._temporary,       { });
    _file      = std::exchange(other._file,      NO_FILE);
    _mode      = std::exchange(other._mode,      NO_MODE);
    _state     = std::exchange(other._state, State::SPENT);
    _failure   = std::exchange(other._failure,         { });
    return *this;
  }

  FileWriter::~FileWriter() noexcept
  { Discard(); }

  auto FileWriter::Write(Bytes bytes) -> void
  {
    RefuseIfDead("write to");
    if (_state != State::OPEN)
      throw std::runtime_error{ std::format(
        "platform: write to a writer that is not open ('{}')",
        PathToString(_target)) };

    // A failed write kills the writer, or a document with a hole in it commits.
    try { WriteToFile(_file, bytes, _temporary); }
    catch (std::exception const& failure) { Fail(failure.what()); throw; }
  }

  auto FileWriter::Commit() -> void
  {
    RefuseIfDead("commit");
    if (_state != State::OPEN) return;         // committed, or spent

    // Anything failing here is still takeable back: the target is untouched.
    try
    {
      ApplyMode(_file, _mode, _temporary);      // umask cannot narrow this one
      SyncFile(_file);                          // the bytes reach the disk
      CloseFile(std::exchange(_file, NO_FILE)); // before the name reaches the
      RenameOver(_temporary, _target);          // directory -- that order is
    }                                           // the guarantee
    catch (std::exception const& failure) { Fail(failure.what()); throw; }

    // The document is saved from here and the state says so before the last call
    // runs: Committed() must already answer yes when the directory sync throws.
    _temporary.clear();
    _state = State::COMMITTED;

    // SyncDirectoryOf knows a directory, not that a rename just landed, so this
    // frame is the one that can say what its failure means.
    try { SyncDirectoryOf(_target); }
    catch (std::exception const& failure)
    {
      throw SavedButNotDurable{ _target, std::format(
        "platform: '{}' IS SAVED, but its directory entry could not be made "
        "durable: {}", PathToString(_target), failure.what()) };
    }
  }

  auto FileWriter::Fail(std::string what) noexcept -> void
  {
    _failure = std::move(what);
    _state   = State::DEAD;
    Discard();
  }

  auto FileWriter::Discard() noexcept -> void
  {
    if (_file != NO_FILE)
      CloseFile(std::exchange(_file, NO_FILE));
    if (!_temporary.empty())
    {
      std::error_code ignored;
      std::filesystem::remove(std::exchange(_temporary, { }), ignored);
    }
  }


  auto FileWriter::RefuseIfDead(std::string_view verb) const -> void
  {
    if (_state != State::DEAD) return;
    throw std::runtime_error{ std::format(
      "platform: cannot {} '{}': the save was abandoned after: {}",
      verb, PathToString(_target), _failure) };
  }
}
