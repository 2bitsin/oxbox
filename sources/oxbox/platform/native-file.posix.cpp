// The OS handle's verbs, POSIX.

#include "oxbox/platform/native-file.hpp"

#include "oxbox/platform/native-file.posix.hpp"
#include "oxbox/utilities/path.hpp"

#include <cerrno>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace oxbox::platform::detail::native_file
{
  auto Failure(std::string_view what,
               std::filesystem::path const& path) -> std::string
  {
    auto const code{ errno };
    return std::format("platform: {} '{}': {}",
                       what, PathToString(path),
                       std::system_category().message(code));
  }

  auto Failure(std::string_view what, std::filesystem::path const& path,
               std::size_t bytes) -> std::string
  {
    auto const code{ errno };
    return std::format("platform: {} '{}' ({:#x} bytes): {}",
                       what, PathToString(path), bytes,
                       std::system_category().message(code));
  }

  auto Failure(std::string_view what) -> std::string
  {
    auto const code{ errno };
    return std::format("platform: {}: {}",
                       what, std::system_category().message(code));
  }

  namespace
  {
    // What a host with no directory fsync answers. EOPNOTSUPP is spelled beside
    // ENOTSUP because they need not be the same number; EPERM is FUSE or an LSM.
    auto Unimplemented(int code) noexcept -> bool
    {
      return code == EINVAL || code == ENOSYS || code == EBADF
          || code == ENOTSUP || code == EOPNOTSUPP || code == EPERM;
    }
  }

  auto ModeOf(std::filesystem::path const& path) -> FileMode
  {
    struct ::stat status{ };
    if (::stat(path.c_str(), &status) != 0) return NO_MODE;
    // the permission and set-id/sticky bits; the type bits are the inode's
    return static_cast<FileMode>(status.st_mode & 07777u);
  }

  auto OpenForWrite(std::filesystem::path const& path) -> OpenedFile
  {
    auto descriptor{ ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL,
                            NEW_FILE_MODE) };
    auto const created{ descriptor >= 0 };
    // only EEXIST is a reason to try again -- see MapFileForWrite
    if (!created && errno != EEXIST)
      throw std::runtime_error{ Failure("cannot create", path) };
    if (!created) descriptor = ::open(path.c_str(), O_WRONLY | O_TRUNC);
    if (descriptor < 0)
      throw std::runtime_error{ Failure("cannot open", path) };
    return { static_cast<NativeFile>(descriptor), created };
  }


  auto CreateExclusiveForWrite(std::filesystem::path const& path,
                               FileMode mode) -> NativeFile
  {
    // Created with the carried mode rather than 0644-then-fix: the umask can
    // only narrow it, so a private file's replacement is never briefly readable.
    auto const born{ mode == NO_MODE ? NEW_FILE_MODE
                                     : static_cast<::mode_t>(mode) };
    auto const descriptor{ ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL,
                                  born) };
    if (descriptor < 0)
      throw std::runtime_error{ Failure("cannot create", path) };
    return static_cast<NativeFile>(descriptor);
  }

  auto ApplyBacking(NativeFile, Backing backing,
                    std::filesystem::path const&) -> Backing
  {
    // ftruncate stores no zeros, so a file given a length it was never written
    // to is already all hole; SPARSE comes straight back because it is granted.
    return backing;
  }

  auto ApplyMode(NativeFile file, FileMode mode,
                 std::filesystem::path const& path) -> void
  {
    if (mode == NO_MODE) return;          // a new file keeps the host default
    // fchmod and not chmod: the descriptor cannot be pointed at another file
    if (::fchmod(Descriptor(file), static_cast<::mode_t>(mode)) != 0)
      throw std::runtime_error{ Failure("cannot set the mode of", path) };
  }

  auto WriteToFile(NativeFile file, Bytes bytes,
                   std::filesystem::path const& path) -> void
  {
    auto remaining{ bytes };
    while (!remaining.empty())
    {
      auto const written{ ::write(Descriptor(file), remaining.data(),
                                  remaining.size()) };
      // <= and not <: a write() reporting zero bytes of a non-empty buffer has
      // made no progress, and a loop reading that as progress spins forever.
      if (written <= 0)
        throw std::runtime_error{ Failure("write to", path) };
      remaining = remaining.subspan(static_cast<std::size_t>(written));
    }
  }

  auto CloseFile(NativeFile file) noexcept -> void
  {
    if (file != NO_FILE) ::close(Descriptor(file));
  }

  auto RenameOver(std::filesystem::path const& from,
                  std::filesystem::path const& to) -> void
  {
    // rename(2) replaces an existing target atomically within one filesystem;
    // across them it is EXDEV rather than a slow copy
    if (::rename(from.c_str(), to.c_str()) != 0)
    {
      auto const code{ errno };
      throw std::runtime_error{ std::format(
        "platform: cannot rename '{}' onto '{}': {}",
        PathToString(from), PathToString(to),
        std::system_category().message(code)) };
    }
  }

  auto SyncFile(NativeFile file) -> void
  {
    // a mapping on this road carries no file: msync(MS_SYNC) has already synced
    if (file == NO_FILE) return;
    if (::fsync(Descriptor(file)) != 0)
      throw std::runtime_error{ Failure("fsync failed") };
  }

  auto SyncDirectoryOf(std::filesystem::path const& path) -> void
  {
    // The rename is in the directory, not the file, so syncing the file does not
    // make the new name durable; a host that cannot is tolerated in silence.
    auto parent{ path.parent_path() };
    if (parent.empty()) parent = ".";

    ScopedFile const directory{ static_cast<NativeFile>(
      ::open(parent.c_str(), O_RDONLY | O_DIRECTORY)) };
    if (directory.Get() == NO_FILE)
    {
      // A drop-box directory (write and search, no read) cannot be opened for
      // reading by anyone; EPERM is how FUSE with default_permissions refuses.
      if (errno == EACCES || errno == EPERM) return;
      throw std::runtime_error{ Failure("cannot open the directory of",
                                        path) };
    }

    if (::fsync(Descriptor(directory.Get())) != 0)
    {
      // vboxsf, 9p and plenty of FUSE have no directory fsync at all.
      if (Unimplemented(errno)) return;
      throw std::runtime_error{ Failure("cannot sync the directory of",
                                        path) };
    }
  }
}
