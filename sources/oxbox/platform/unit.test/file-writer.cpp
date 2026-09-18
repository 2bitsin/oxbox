// The save road: a file that appears whole or not at all -- every case here is
// about the moment between "started writing" and "finished".

#include "oxbox/platform/file-writer.hpp"
#include "oxbox/platform/mapped-file.hpp"

#include "fixtures.hpp"
#include "oxbox/utilities/path.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <array>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace oxbox;

namespace native = oxbox::platform::detail::native_file;
using namespace std::string_view_literals;

using platform::FileWriter;
using platform::MapIntent;
using platform::MappedFile;
using platform::WriteBinaryFile;
using platform::SavedButNotDurable;
using platform::test::Contains;
using platform::test::EntryCount;
using platform::test::ReadSample;
using platform::test::ReasonOf;
using platform::test::TempPath;
using platform::test::WriteSample;
using utilities::AsBytes;
using utilities::Bytes;
using utilities::PathToString;

namespace
{
}

TEST(FileWriter, ChunksArriveInOrderAndTheWholeThingLands)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "document.bin" };

  FileWriter writer{ target };
  writer.Write(AsBytes("one "sv));
  writer.Write(AsBytes("two "sv));
  writer.Write(AsBytes("three"sv));
  writer.Commit();

  EXPECT_EQ(ReadSample(target), "one two three");
  EXPECT_EQ(writer.Path(), target);
}

TEST(FileWriter, ADocumentIsSavedWithoutEverBeingContiguous)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "large.bin" };
  constexpr std::size_t CHUNK{ std::size_t{ 1u } << 20u };   // 1 MiB
  constexpr std::size_t CHUNKS{ 8u };
  std::string const piece(CHUNK, 'x');

  FileWriter writer{ target };
  for ([[maybe_unused]] auto const round : std::views::iota(0u, CHUNKS))
    writer.Write(AsBytes(std::string_view{ piece }));
  writer.Commit();

  ASSERT_TRUE(std::filesystem::exists(target));
  EXPECT_EQ(std::filesystem::file_size(target), CHUNK * CHUNKS);
}

TEST(FileWriter, TheTargetDoesNotExistUntilCommit)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "pending.bin" };

  FileWriter writer{ target };
  writer.Write(AsBytes("half a document"sv));
  EXPECT_FALSE(std::filesystem::exists(target))
    << "a reader could see a file that is still being written";

  writer.Commit();
  EXPECT_TRUE(std::filesystem::exists(target));
}

TEST(FileWriter, AWriterThatIsNeverCommittedLeavesTheTargetExactlyAsItWas)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "precious.bin" };
  WriteSample(target, "the version on disk"sv);

  {
    FileWriter writer{ target };
    writer.Write(AsBytes("a replacement nobody asked to keep"sv));
  }                                            // no Commit: the save is off

  EXPECT_EQ(ReadSample(target), "the version on disk");
}

TEST(FileWriter, AWriterThatIsNeverCommittedLeavesNoTemporaryBehind)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const target{ directory / "abandoned.bin" };

  {
    FileWriter writer{ target };
    writer.Write(AsBytes("scratch"sv));
    EXPECT_EQ(EntryCount(directory), 1u) << "the temporary should be the only "
                                            "file here while writing";
  }

  EXPECT_EQ(EntryCount(directory), 0u) << "a temporary outlived its writer";
}

TEST(FileWriter, CommitReplacesAnExistingFileWholeAndLeavesNothingBeside)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const target{ directory / "document.bin" };
  WriteSample(target, "the old and much longer version"sv);

  {
    FileWriter writer{ target };
    writer.Write(AsBytes("the new one"sv));
    writer.Commit();
  }

  EXPECT_EQ(ReadSample(target), "the new one");
  EXPECT_EQ(EntryCount(directory), 1u) << "the temporary survived the rename";
}

TEST(FileWriter, MovingAWriterMovesTheSaveAndCommitsItOnce)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const target{ directory / "moved.bin" };

  FileWriter source{ target };
  source.Write(AsBytes("carried across"sv));
  FileWriter landed{ std::move(source) };
  landed.Commit();

  EXPECT_EQ(ReadSample(target), "carried across");
  EXPECT_EQ(EntryCount(directory), 1u);
  EXPECT_NO_THROW(source.Commit());   // moved from: nothing left to commit
}

TEST(FileWriter, WritingThroughACommittedWriterIsRefusedRatherThanIgnored)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "done.bin" };

  FileWriter writer{ target };
  writer.Write(AsBytes("all of it"sv));
  writer.Commit();

  EXPECT_THROW(writer.Write(AsBytes("more"sv)), std::runtime_error);
  EXPECT_EQ(ReadSample(target), "all of it");
}

TEST(FileWriter, AWriterThatCannotOpenItsTemporaryFailsByNameAndReason)
{
  TempPath scratch;
  auto const nowhere{ scratch.Path() / "absent" / "document.bin" };
  try {
    FileWriter writer{ nowhere };
    FAIL() << "expected a throw";
  } catch (std::runtime_error const& error) {
    EXPECT_TRUE(Contains(error.what(), "cannot create")) << error.what();
    // the temporary's name, which starts with the target's
    EXPECT_TRUE(Contains(error.what(), PathToString(nowhere))) << error.what();
    EXPECT_FALSE(ReasonOf(error.what()).empty()) << error.what();
  }
}

// A directory standing where the target should go makes the rename fail on every
// host (EISDIR on posix, win32 refuses it too) without touching permissions.
TEST(FileWriter, AFailedRenameKillsTheWriterAndSavesNothing)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const target{ directory / "in-the-way" };
  std::filesystem::create_directory(target);       // a rename cannot land here

  std::string first;
  std::string second;
  {
    FileWriter writer{ target };
    writer.Write(AsBytes("a document that will not land"sv));
    try { writer.Commit(); } catch (std::runtime_error const& error)
    { first = error.what(); }

    try { writer.Commit(); } catch (std::runtime_error const& error)
    { second = error.what(); }
  }

  ASSERT_FALSE(first.empty())  << "the rename was expected to fail";
  ASSERT_FALSE(second.empty()) << "a second commit reported success after a "
                                  "save that never happened";
  EXPECT_TRUE(Contains(first,  "cannot rename")) << first;
  EXPECT_TRUE(Contains(second, "the save was abandoned")) << second;
  EXPECT_TRUE(Contains(second, "cannot rename")) << second;   // and which one

  EXPECT_TRUE(std::filesystem::is_directory(target))
    << "the thing standing in the way was disturbed";
  EXPECT_EQ(EntryCount(directory), 1u) << "a dead writer left its temporary";
}

TEST(FileWriter, CommittingTwiceIsANoOpNotASecondSave)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const target{ directory / "document.bin" };
  WriteSample(target, "the good version"sv);

  FileWriter writer{ target };
  writer.Write(AsBytes("a replacement"sv));
  writer.Commit();
  EXPECT_EQ(ReadSample(target), "a replacement");

  // committing twice is a no-op, not a second save
  EXPECT_NO_THROW(writer.Commit());
  EXPECT_EQ(EntryCount(directory), 1u);
}

// Commit() throws on both sides of the rename, and a consumer has to tell "keep
// the edits" from "reopen the file" without calling Commit() twice to find out.

TEST(FileWriter, CommittedIsFalseUntilTheRenameLandsAndTrueAfter)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "document.bin" };

  FileWriter writer{ target };
  EXPECT_FALSE(writer.Committed());
  writer.Write(AsBytes("a document"sv));
  EXPECT_FALSE(writer.Committed()) << "written is not saved";

  writer.Commit();
  EXPECT_TRUE(writer.Committed());
  EXPECT_TRUE(writer.Committed()) << "asking twice changed the answer";
}

TEST(FileWriter, AMovedFromWriterSavedNothingAndSaysSo)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "moved.bin" };

  FileWriter source{ target };
  source.Write(AsBytes("carried across"sv));
  FileWriter landed{ std::move(source) };

  // nothing left to do is not the same fact as having committed
  EXPECT_FALSE(source.Committed());
  EXPECT_FALSE(landed.Committed());

  landed.Commit();
  EXPECT_TRUE(landed.Committed());
  EXPECT_FALSE(source.Committed());
}

TEST(FileWriter, MoveAssignmentLeavesTheSourceOwningAndClaimingNothing)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const saved{ directory / "saved.bin" };
  auto const pending{ directory / "pending.bin" };

  FileWriter destination{ saved };
  destination.Write(AsBytes("a committed document"sv));
  destination.Commit();
  ASSERT_TRUE(destination.Committed());

  FileWriter source{ pending };
  source.Write(AsBytes("not committed"sv));

  destination = std::move(source);

  EXPECT_FALSE(source.Committed()) << "the source inherited a commit";
  EXPECT_TRUE(source.Path().empty())
    << "the source named a document it does not own: " << source.Path();
  EXPECT_EQ(destination.Path(), pending);
  EXPECT_FALSE(destination.Committed());

  // the writer that was assigned over let its temporary go then, not later
  EXPECT_EQ(EntryCount(directory), 2u)   // the saved document, and the temp
    << "assigning over a writer left an extra file behind";

  destination.Commit();
  EXPECT_EQ(ReadSample(pending), "not committed");
  EXPECT_EQ(ReadSample(saved),   "a committed document");
}

TEST(FileWriter, MoveAssigningAWriterToItselfKeepsIt)
{
  TempPath scratch;
  auto const target{ scratch.Directory() / "self.bin" };

  FileWriter writer{ target };
  writer.Write(AsBytes("still here"sv));

  auto& alias{ writer };
  writer = std::move(alias);           // the discard must not eat its own

  EXPECT_EQ(writer.Path(), target);
  writer.Commit();
  EXPECT_TRUE(writer.Committed());
  EXPECT_EQ(ReadSample(target), "still here");
}

TEST(FileWriter, AWriterKilledBeforeTheRenameIsNotCommitted)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };
  auto const target{ directory / "in-the-way" };
  std::filesystem::create_directory(target);       // a rename cannot land here

  FileWriter writer{ target };
  writer.Write(AsBytes("a document that will not land"sv));
  EXPECT_THROW(writer.Commit(), std::runtime_error);
  EXPECT_FALSE(writer.Committed()) << "a failed save reported itself saved";

  // and the failure is not the after-the-rename kind
  try { writer.Commit(); FAIL() << "expected a throw"; }
  catch (SavedButNotDurable const& saved)
  { FAIL() << "a save that never happened threw the saved-anyway type: "
           << saved.what(); }
  catch (std::runtime_error const&) { }        // the expected shape
}

TEST(FileWriter, TheSavedAnywayFailureCarriesThePathAndIsARuntimeError)
{
  // It is a runtime_error, so an existing broad catch still sees it, and it
  // carries the path so a caller can word the failure without parsing what().
  static_assert(std::is_base_of_v<std::runtime_error, SavedButNotDurable>,
                "an existing catch (std::runtime_error const&) must still "
                "see this one");
  SavedButNotDurable const failure{ "/tmp/document.bin", "the message" };
  EXPECT_EQ(failure.path, std::filesystem::path{ "/tmp/document.bin" });
  EXPECT_STREQ(failure.what(), "the message");
}

// The non-atomic road: a cache file, anything a caller would simply make again.

TEST(WriteBinaryFile, WriteBinaryFileTruncatesAndLandsBytes)
{
  TempPath fresh;
  constexpr std::array PAYLOAD{ std::byte{ 1u }, std::byte{ 2u },
                                std::byte{ 3u } };
  WriteBinaryFile(fresh.Path(), PAYLOAD);
  {
    MappedFile const check{ fresh.Path(), MapIntent::READ_ONLY };
    ASSERT_EQ(check.size(), 3u);
    EXPECT_EQ(check[2u], std::byte{ 3u });
  }                     // scoped: the rewrite opens with no sharing at all
  WriteBinaryFile(fresh.Path(), Bytes{ PAYLOAD }.first(1u));
  MappedFile const again{ fresh.Path(), MapIntent::READ_ONLY };
  EXPECT_EQ(again.size(), 1u);
}

TEST(WriteBinaryFile, WriteBinaryFileNamesAPathItCannotCreate)
{
  TempPath sample;
  auto const nowhere{ sample.Path() / "absent" / "dump.bin" };
  constexpr std::array<std::byte, 4u> payload{ };
  try {
    WriteBinaryFile(nowhere, Bytes{ payload });
    FAIL() << "expected a throw";
  } catch (std::runtime_error const& error) {
    EXPECT_TRUE(Contains(error.what(), "cannot create"))       << error.what();
    EXPECT_TRUE(Contains(error.what(), PathToString(nowhere))) << error.what();
    EXPECT_FALSE(ReasonOf(error.what()).empty())               << error.what();
  }
}
TEST(WriteBinaryFile, TheWriteSubstrateHandsBackAFileToWriteThrough)
{
  // CreateExclusiveForWrite returns a descriptor, so an answer about backing is
  // the one thing it could not give back; a caller calls ApplyBacking on it.
  TempPath scratch;
  auto const path{ scratch.Directory() / "streamed.img" };

  auto const file{ native::CreateExclusiveForWrite(path,
                                                   native::NO_MODE) };
  ASSERT_NE(file, native::NO_FILE);
  constexpr std::array PAYLOAD{ std::byte{ 7u } };
  native::WriteToFile(file, PAYLOAD, path);
  native::CloseFile(file);

  MappedFile const check{ path, MapIntent::READ_ONLY };
  ASSERT_EQ(check.size(), 1u);
  EXPECT_EQ(check[0u], std::byte{ 7u });
}
