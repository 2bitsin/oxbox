#pragma once
// FileWriter renames a temporary over the target so no reader sees a torn file.

#include "oxbox/platform/native-file.hpp"

#include "oxbox/utilities/short-types.hpp"
#include "oxbox/utilities/span.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace oxbox::platform::detail::file_writer
{
  using namespace utilities;
  using namespace native_file;

  // The rename landed: the document is saved and only its name is not durable.
  struct SavedButNotDurable : std::runtime_error
  {
    SavedButNotDurable(std::filesystem::path const& saved,
                       std::string const&           what)
    : std::runtime_error{ what }
    , path{ saved }
    {
    }

    std::filesystem::path path;   // the document, which is on disk
  };

  // Not atomic: a failure after the truncate leaves an existing target short.
  auto WriteBinaryFile(std::filesystem::path const& path, Bytes bytes) -> void;

  // The temporary is beside the target because a rename is atomic only within
  // one filesystem. The target's mode carries over; ownership, ACLs and hard
  // links stay with the old inode, and on win32 a READONLY target refuses the
  // replacing rename with ACCESS_DENIED.
  class FileWriter
  {
  public:
    explicit FileWriter(std::filesystem::path const& path);

    FileWriter(FileWriter const&)                    = delete;
    auto operator = (FileWriter const&) -> FileWriter& = delete;

    FileWriter(FileWriter&& other) noexcept;
    auto operator = (FileWriter&& other) noexcept -> FileWriter&;

    ~FileWriter() noexcept;

    // A failed write kills the writer: every later call refuses.
    auto Write(Bytes bytes) -> void;

    // On win32 a live view of the target blocks the replacing rename.
    auto Commit() -> void;

    auto Path() const noexcept -> std::filesystem::path const& { return _target; }

    // True from the rename on, SavedButNotDurable included.
    auto Committed() const noexcept -> bool
    { return _state == State::COMMITTED; }

  private:
    enum class State : U08
    {
      OPEN,       // taking writes
      COMMITTED,  // the rename landed: the target is the document
      SPENT,      // moved from -- owns nothing, saved nothing
      DEAD        // something failed; _failure says what
    };

    auto Discard     ()                            noexcept -> void;
    auto Fail        (std::string what)            noexcept -> void;
    auto RefuseIfDead(std::string_view verb) const -> void;

    std::filesystem::path _target   { };
    std::filesystem::path _temporary{ };
    NativeFile            _file     { NO_FILE };
    FileMode              _mode     { NO_MODE };
    State                 _state    { State::OPEN };
    std::string           _failure  { };   // what killed it, empty while alive
  };
}

namespace oxbox::platform
{
  using detail::file_writer::FileWriter;
  using detail::file_writer::SavedButNotDurable;
  using detail::file_writer::WriteBinaryFile;
}
