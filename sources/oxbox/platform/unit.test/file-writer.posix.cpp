// The save road's posix-shaped contracts: the mode it carries, and what it does
// when a write fails part way. The mechanisms are posix -- permission bits, and
// RLIMIT_FSIZE as a way to make a write fail on demand -- the contracts are not.

#include "oxbox/platform/file-writer.hpp"

#include "fixtures.hpp"
#include "privilege.hpp"

#include <gtest/gtest.h>

#include <csignal>
#include <cstddef>
#include <filesystem>
#include <ios>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>

using namespace oxbox;
using namespace std::string_view_literals;

using platform::FileWriter;
using platform::WriteBinaryFile;
using platform::SavedButNotDurable;
using platform::test::Contains;
using platform::test::EntryCount;
using platform::test::ReadSample;
using platform::test::RestrictedDirectory;
using platform::test::TempPath;
using platform::test::WriteSample;
using utilities::AsBytes;

namespace
{
  auto ModeBitsOf(std::filesystem::path const& path) -> ::mode_t
  {
    struct ::stat status{ };
    return ::stat(path.c_str(), &status) == 0 ? (status.st_mode & 07777u) : 0u;
  }

  // RLIMIT_FSIZE lets a write succeed until the file reaches a size and refuse
  // after, which /dev/full cannot express. SIGXFSZ has to be ignored: its
  // default action kills the process, and the test wants write()'s EFBIG.
  class FileSizeLimit
  {
  public:
    explicit FileSizeLimit(std::size_t ceiling)
    {
      _oversize = ::signal(SIGXFSZ, SIG_IGN);
      if (_oversize == SIG_ERR) { _refusal = "signal(SIGXFSZ)"; return; }
      if (::getrlimit(RLIMIT_FSIZE, &_saved) != 0)
      { _refusal = "getrlimit(RLIMIT_FSIZE)"; return; }
      auto tightened{ _saved };
      tightened.rlim_cur = static_cast<::rlim_t>(ceiling);
      if (::setrlimit(RLIMIT_FSIZE, &tightened) != 0)
      { _refusal = "setrlimit(RLIMIT_FSIZE)"; return; }
      _capped = true;
    }

    FileSizeLimit(FileSizeLimit const&)                    = delete;
    auto operator = (FileSizeLimit const&) -> FileSizeLimit& = delete;
    FileSizeLimit(FileSizeLimit&&)                         = delete;
    auto operator = (FileSizeLimit&&) -> FileSizeLimit&      = delete;

    ~FileSizeLimit() noexcept
    {
      if (_capped) ::setrlimit(RLIMIT_FSIZE, &_saved);
      if (_oversize != SIG_ERR) ::signal(SIGXFSZ, _oversize);
    }

    // empty when the ceiling really is in place
    auto Refusal() const noexcept -> std::string_view { return _refusal; }

  private:
    ::rlimit            _saved   { };
    void              (*_oversize)(int){ SIG_ERR };
    bool                _capped  { false };
    std::string_view    _refusal { };
  };

  // Three because stdin, stdout and stderr normally hold 0, 1 and 2 --
  // LowDescriptorsHeld makes that true rather than assuming it.
  constexpr std::size_t DESCRIPTOR_CEILING{ 3u };

  // Constructed before the writer: open() hands out the lowest free number, so
  // otherwise the writer's temporary takes one, and Commit closes it before the
  // directory sync -- handing the sync back the slot the ceiling must deny it.
  class LowDescriptorsHeld
  {
  public:
    explicit LowDescriptorsHeld(std::size_t ceiling)
    {
      while (true)
      {
        auto const held{ ::open("/dev/null", O_RDWR) };
        if (held < 0) { _refusal = "open(/dev/null)"; return; }
        if (static_cast<std::size_t>(held) >= ceiling)
        { ::close(held); break; }            // everything below is taken now
        _held.push_back(held);
      }
    }

    LowDescriptorsHeld(LowDescriptorsHeld const&)                    = delete;
    auto operator = (LowDescriptorsHeld const&) -> LowDescriptorsHeld& = delete;
    LowDescriptorsHeld(LowDescriptorsHeld&&)                         = delete;
    auto operator = (LowDescriptorsHeld&&) -> LowDescriptorsHeld&      = delete;

    ~LowDescriptorsHeld() noexcept
    { for (auto const held : _held) ::close(held); }

    // empty when every number below the ceiling really is held
    auto Refusal() const noexcept -> std::string_view { return _refusal; }

  private:
    std::vector<int> _held   { };   // the holes that had to be filled, if any
    std::string_view _refusal{ };
  };

  // Applied after the writer has its descriptor: fchmod, fsync, close and
  // rename need no new one, so this refuses the directory sync's open alone.
  class DescriptorLimit
  {
  public:
    explicit DescriptorLimit(std::size_t ceiling)
    {
      if (::getrlimit(RLIMIT_NOFILE, &_saved) != 0)
      { _refusal = "getrlimit(RLIMIT_NOFILE)"; return; }
      auto tightened{ _saved };
      tightened.rlim_cur = static_cast<::rlim_t>(ceiling);
      if (::setrlimit(RLIMIT_NOFILE, &tightened) != 0)
      { _refusal = "setrlimit(RLIMIT_NOFILE)"; return; }
      _capped = true;
    }

    DescriptorLimit(DescriptorLimit const&)                    = delete;
    auto operator = (DescriptorLimit const&) -> DescriptorLimit& = delete;
    DescriptorLimit(DescriptorLimit&&)                         = delete;
    auto operator = (DescriptorLimit&&) -> DescriptorLimit&      = delete;

    ~DescriptorLimit() noexcept
    { if (_capped) ::setrlimit(RLIMIT_NOFILE, &_saved); }

    // empty when the ceiling really is in place
    auto Refusal() const noexcept -> std::string_view { return _refusal; }

  private:
    ::rlimit         _saved  { };
    bool             _capped { false };
    std::string_view _refusal{ };
  };
}

TEST(FileWriter, TheTargetsModeSurvivesTheSave)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "program" };
  WriteSample(target, "#!/bin/sh\n"sv);
  std::filesystem::permissions(target, std::filesystem::perms{ 0755 });
  ASSERT_EQ(ModeBitsOf(target), 0755u);

  {
    FileWriter writer{ target };
    writer.Write(AsBytes("#!/bin/sh\necho replaced\n"sv));
    writer.Commit();
  }

  EXPECT_EQ(ModeBitsOf(target), 0755u)
    << "a save turned an executable into a plain file";
  EXPECT_TRUE(Contains(ReadSample(target), "replaced"));
}

TEST(FileWriter, APrivateTargetIsNotPublishedByBeingSaved)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "secret.key" };
  WriteSample(target, "old secret"sv);
  std::filesystem::permissions(target, std::filesystem::perms{ 0600 });
  ASSERT_EQ(ModeBitsOf(target), 0600u);

  {
    FileWriter writer{ target };
    writer.Write(AsBytes("new secret"sv));
    writer.Commit();
  }

  EXPECT_EQ(ModeBitsOf(target), 0600u) << "a save published a private file";
}

TEST(FileWriter, ANewTargetIsBornWithTheHostDefaultRatherThanNothing)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "fresh.bin" };   // nothing to carry

  FileWriter writer{ target };
  writer.Write(AsBytes("hello"sv));
  writer.Commit();

  // whatever the umask makes of 0644: readable and owner-writable
  auto const mode{ ModeBitsOf(target) };
  EXPECT_NE(mode & 0400u, 0u) << std::oct << mode;
  EXPECT_NE(mode & 0200u, 0u) << std::oct << mode;
}

TEST(FileWriter, AWriteThatFailsKillsTheWriterAndTheTargetIsUntouched)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "document.bin" };
  WriteSample(target, "the good version"sv);

  constexpr std::size_t CEILING{ 4096u };
  std::vector<std::byte> const chunk(CEILING * 4u, std::byte{ 'x' });

  std::string_view refusal;
  std::string      write_failure;
  {
    FileSizeLimit const capped{ CEILING };
    refusal = capped.Refusal();
    if (refusal.empty())
    {
      FileWriter writer{ target };
      try { writer.Write(AsBytes(chunk)); }
      catch (std::runtime_error const& error) { write_failure = error.what(); }

      EXPECT_THROW(writer.Commit(), std::runtime_error);
    }
  }

  ASSERT_TRUE(refusal.empty()) << "no file size ceiling: " << refusal
                               << " was refused";
  ASSERT_FALSE(write_failure.empty()) << "the write was expected to fail";
  EXPECT_TRUE(Contains(write_failure, "write to")) << write_failure;

  EXPECT_EQ(ReadSample(target), "the good version")
    << "a failed save replaced a good file with a partial one";
}

TEST(FileWriter, ADeadWriterSaysWhatKilledItRatherThanThatItIsClosed)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const target{ directory / "document.bin" };
  constexpr std::size_t CEILING{ 4096u };
  std::vector<std::byte> const chunk(CEILING * 4u, std::byte{ 'x' });

  std::string_view refusal;
  std::string      refused;
  {
    FileSizeLimit const capped{ CEILING };
    refusal = capped.Refusal();
    if (refusal.empty())
    {
      FileWriter writer{ target };
      EXPECT_THROW(writer.Write(AsBytes(chunk)), std::runtime_error);
      try { writer.Commit(); } catch (std::runtime_error const& error)
      { refused = error.what(); }
    }
  }

  ASSERT_TRUE(refusal.empty()) << refusal;
  ASSERT_FALSE(refused.empty()) << "the commit was expected to be refused";
  EXPECT_TRUE(Contains(refused, "the save was abandoned")) << refused;
  EXPECT_TRUE(Contains(refused, "write to")) << refused;   // and which one

  EXPECT_FALSE(std::filesystem::exists(target))
    << "a killed writer left its target behind";
  EXPECT_EQ(EntryCount(directory), 0u)
    << "a killed writer left its temporary behind";
}

// A drop-box directory (write and search, no read) cannot be opened for reading
// by anyone, and a shared folder may have no directory fsync at all. 0333 rather
// than 0300 so the restriction binds root too, once the effective uid drops.
TEST(FileWriter, ADirectoryThatCannotBeReadDoesNotTurnASaveIntoAFailure)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const target{ directory / "document.bin" };

  std::string refusal;
  std::string thrown;
  {
    FileWriter writer{ target };                 // temporary made while open
    writer.Write(AsBytes("the whole document"sv));

    RestrictedDirectory const dropbox{ directory,
                                       std::filesystem::perms{ 0333 } };
    refusal = dropbox.Refusal();
    if (refusal.empty())
      try { writer.Commit(); } catch (std::exception const& error)
      { thrown = error.what(); }
  }

  if (!refusal.empty())
    GTEST_SKIP() << "cannot make a directory this process may not read: "
                 << refusal;
  EXPECT_TRUE(thrown.empty())
    << "a save that landed was reported as a failure: " << thrown;
  EXPECT_EQ(ReadSample(target), "the whole document");
}

// Every failure the directory sync tolerates is silent by design, so the one
// case that reaches the caller is EMFILE: with the ceiling below the lowest free
// number, only the sync's own open is refused and the document still lands.
TEST(FileWriter, ADurabilityFailureAfterTheRenameSaysTheDocumentIsSaved)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "document.bin" };

  std::string_view refusal;
  std::string      reported;
  std::filesystem::path carried;
  bool                  committed{ false };
  {
    // held first, so the writer's own descriptor is one the ceiling excludes
    LowDescriptorsHeld const held{ DESCRIPTOR_CEILING };
    refusal = held.Refusal();

    FileWriter writer{ target };
    writer.Write(AsBytes("the whole document"sv));

    if (refusal.empty())
    {
      DescriptorLimit const starved{ DESCRIPTOR_CEILING };
      refusal = starved.Refusal();
      if (refusal.empty())
        try { writer.Commit(); } catch (SavedButNotDurable const& saved)
        { reported = saved.what(); carried = saved.path; }
    }
    committed = writer.Committed();
  }

  ASSERT_TRUE(refusal.empty()) << "no descriptor ceiling: " << refusal;
  ASSERT_FALSE(reported.empty())
    << "the directory sync was expected to fail with SavedButNotDurable";
  EXPECT_TRUE(Contains(reported, "IS SAVED")) << reported;
  EXPECT_EQ(carried, target) << "the failure did not carry the document";

  EXPECT_TRUE(committed) << "a saved document reported itself unsaved";
  EXPECT_EQ(ReadSample(target), "the whole document");
}

// This road truncates before it writes, so a failure leaves an existing target
// short -- the documented cost of the non-atomic road; it must not delete it.
TEST(WriteBinaryFile, AFailedWriteTakesTheFileItCreatedWithIt)
{
  TempPath scratch;
  auto const fresh{ scratch.Directory() / "dump.bin" };
  constexpr std::size_t CEILING{ 4096u };
  std::vector<std::byte> const payload(CEILING * 4u, std::byte{ 'x' });

  std::string_view refusal;
  std::string      failure;
  {
    FileSizeLimit const capped{ CEILING };
    refusal = capped.Refusal();
    if (refusal.empty())
      try { platform::WriteBinaryFile(fresh, AsBytes(payload)); }
      catch (std::runtime_error const& error) { failure = error.what(); }
  }

  ASSERT_TRUE(refusal.empty()) << "no file size ceiling: " << refusal;
  ASSERT_FALSE(failure.empty()) << "the write was expected to fail";
  EXPECT_FALSE(std::filesystem::exists(fresh))
    << "a failed write left the file it had just created";
}

TEST(WriteBinaryFile, AFailedWriteLeavesAFileItDidNotCreateWhereItWas)
{
  TempPath scratch;
  auto const existing{ scratch.Directory() / "dump.bin" };
  WriteSample(existing, "what was there before"sv);
  constexpr std::size_t CEILING{ 4096u };
  std::vector<std::byte> const payload(CEILING * 4u, std::byte{ 'x' });

  std::string_view refusal;
  {
    FileSizeLimit const capped{ CEILING };
    refusal = capped.Refusal();
    if (refusal.empty())
      EXPECT_THROW(platform::WriteBinaryFile(existing, AsBytes(payload)),
                   std::runtime_error);
  }

  ASSERT_TRUE(refusal.empty()) << "no file size ceiling: " << refusal;
  EXPECT_TRUE(std::filesystem::exists(existing))
    << "a failed write deleted a file it did not create";
}

// A save replaces the link with a regular file rather than writing through it,
// and the mode that lands is the link target's, because ModeOf stats not lstats.
TEST(FileWriter, ASavedSymlinkBecomesARegularFileWearingItsTargetsMode)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const real{ directory / "real.bin" };
  auto const link{ directory / "link.bin" };

  WriteSample(real, "the file the link points at"sv);
  std::filesystem::permissions(real, std::filesystem::perms{ 0640 });
  std::filesystem::create_symlink(real, link);
  ASSERT_TRUE(std::filesystem::is_symlink(link));

  {
    FileWriter writer{ link };
    writer.Write(AsBytes("saved over the link"sv));
    writer.Commit();
  }

  EXPECT_FALSE(std::filesystem::is_symlink(link))
    << "the link survived a save, so the save went through it";
  EXPECT_EQ(ReadSample(link), "saved over the link");
  EXPECT_EQ(ModeBitsOf(link), 0640u)
    << "the replacement did not inherit the link target's mode";

  // the file the link pointed at is untouched, the surprising half
  EXPECT_EQ(ReadSample(real), "the file the link points at");
}
