#include "oxbox/platform/mapped-file.hpp"

#include "fixtures.hpp"
#include "oxbox/utilities/path.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace oxbox;

namespace native = oxbox::platform::detail::native_file;
using namespace std::string_view_literals;

using platform::Backing;
using platform::MapIntent;
using platform::MappedFile;
using platform::test::ByteAt;
using platform::test::Contains;
using platform::test::ReadSample;
using platform::test::ReasonOf;
using platform::test::TempPath;
using platform::test::WriteSample;
using utilities::PathToString;

TEST(MappedFile, ReadOnlySeesTheFile)
{
  TempPath sample;
  WriteSample(sample.Path(), "hello mapping"sv);
  MappedFile const file{ sample.Path(), MapIntent::READ_ONLY };
  ASSERT_EQ(file.size(), 13u);
  EXPECT_EQ(ByteAt(file, 0u), 'h');
  EXPECT_EQ(ByteAt(file, 12u), 'g');
}

TEST(MappedFile, SharedWritesPersist)
{
  TempPath sample;
  WriteSample(sample.Path(), "abcdef"sv);
  {
    MappedFile file{ sample.Path(), MapIntent::READ_WRITE };
    file[0u] = std::byte{ 'X' };
    file.Flush();
  }
  MappedFile const check{ sample.Path(), MapIntent::READ_ONLY };
  EXPECT_EQ(ByteAt(check, 0u), 'X');
  EXPECT_EQ(ByteAt(check, 1u), 'b');
}

TEST(MappedFile, PrivateCopyLeavesTheFileAlone)
{
  TempPath sample;
  WriteSample(sample.Path(), "abcdef"sv);
  {
    MappedFile file{ sample.Path(), MapIntent::COPY_ON_WRITE };
    file[0u] = std::byte{ 'X' };
    EXPECT_EQ(ByteAt(file, 0u), 'X');                    // the view changed
  }
  MappedFile const check{ sample.Path(), MapIntent::READ_ONLY };
  EXPECT_EQ(ByteAt(check, 0u), 'a');                     // the file did not
}

TEST(MappedFile, EmptyFileMapsEmpty)
{
  TempPath sample;
  WriteSample(sample.Path(), ""sv);
  MappedFile const file{ sample.Path(), MapIntent::READ_ONLY };
  EXPECT_TRUE(file.empty());
}

TEST(MappedFile, MoveTransfersOwnership)
{
  TempPath sample;
  WriteSample(sample.Path(), "abc"sv);
  MappedFile first{ sample.Path(), MapIntent::READ_ONLY };
  MappedFile const second{ std::move(first) };
  EXPECT_TRUE(first.empty());
  EXPECT_EQ(second.size(), 3u);
}

TEST(MappedFile, MissingFileThrows)
{
  TempPath ghost;
  EXPECT_THROW((MappedFile{ ghost.Path(), MapIntent::READ_ONLY }),
               std::runtime_error);
}

TEST(MappedFile, WriteRoadCreatesSizesAndPersists)
{
  TempPath fresh;
  {
    MappedFile state{ fresh.Path(), 64u };                 // created at size
    EXPECT_EQ(state.size(), 64u);
    state[0u] = std::byte{ 0x5A };
    state[63u] = std::byte{ 0xA5 };
  }                                                      // unmap = saved
  {
    MappedFile const check{ fresh.Path(), MapIntent::READ_ONLY };
    ASSERT_EQ(check.size(), 64u);
    EXPECT_EQ(check[0u],  std::byte{ 0x5A });
    EXPECT_EQ(check[63u], std::byte{ 0xA5 });
  }                     // the read view goes before the reopen: a live win32
                        // section keeps the file open FILE_SHARE_READ
  MappedFile const reopened{ fresh.Path(), 64u };          // reopen keeps bytes
  EXPECT_EQ(reopened[0u], std::byte{ 0x5A });
}

TEST(MappedFile, CreatingAtSizeZeroMakesAnEmptyFileAndAnEmptyMapping)
{
  TempPath fresh;
  {
    MappedFile nothing{ fresh.Path(), 0u };
    EXPECT_TRUE(nothing.empty());
    EXPECT_EQ(nothing.size(), 0u);
    EXPECT_NO_THROW(nothing.Flush());
  }
  ASSERT_TRUE(std::filesystem::exists(fresh.Path()));
  EXPECT_EQ(std::filesystem::file_size(fresh.Path()), 0u);
}

// A failure has to arrive as a runtime_error naming the path and the reason:
// "cannot open" is the same sentence for a file that is missing and a file that
// is not ours, and those want two different fixes.

TEST(MappedFile, OpeningAMissingFileNamesIt)
{
  TempPath sample;
  auto const missing{ sample.Path() / "no-such-file.img" };
  try {
    MappedFile const file{ missing, MapIntent::READ_ONLY };
    FAIL() << "expected a throw";
  } catch (std::runtime_error const& error) {
    EXPECT_TRUE(Contains(error.what(), PathToString(missing))) << error.what();
  }
}

// Nothing here spells a strerror string: that text moves with the platform.
TEST(MappedFile, TwoDifferentFailuresOfOneCallCarryTwoDifferentReasons)
{
  TempPath sample;
  auto const& directory{ sample.Directory() };

  std::string absent;
  try { MappedFile const file{ directory / "gone.img", MapIntent::READ_ONLY };
        FAIL() << "expected a throw"; }
  catch (std::runtime_error const& error) { absent = error.what(); }

  // a directory opens on posix for reading and refuses O_RDWR; win32 refuses
  // it without FILE_FLAG_BACKUP_SEMANTICS
  std::string wrong_kind;
  try { MappedFile const file{ directory, MapIntent::READ_WRITE };
        FAIL() << "expected a throw"; }
  catch (std::runtime_error const& error) { wrong_kind = error.what(); }

  EXPECT_TRUE(Contains(absent,     "cannot open")) << absent;
  EXPECT_TRUE(Contains(wrong_kind, "cannot open")) << wrong_kind;
  EXPECT_FALSE(ReasonOf(absent).empty())     << absent;
  EXPECT_FALSE(ReasonOf(wrong_kind).empty()) << wrong_kind;
  EXPECT_NE(ReasonOf(absent), ReasonOf(wrong_kind))
    << absent << "\n" << wrong_kind;
}

TEST(MappedFile, CreatingUnderAMissingDirectoryFailsByName)
{
  TempPath sample;
  auto const nowhere{ sample.Path() / "absent" / "new.img" };
  try {
    MappedFile const file{ nowhere, 4096u };
    FAIL() << "expected a throw";
  } catch (std::runtime_error const& error) {
    EXPECT_TRUE(Contains(error.what(), PathToString(nowhere))) << error.what();
    EXPECT_FALSE(ReasonOf(error.what()).empty()) << error.what();
  }
}

TEST(MappedFile, ANonsenseSizeIsRefusedBeforeAnythingIsMapped)
{
  // The sizing call takes a signed length on both roads -- ftruncate's off_t
  // and SetFilePointerEx's LARGE_INTEGER -- so a size with the top bit set
  // arrives negative and the call fails.
  TempPath sample;
  auto const path{ sample.Directory() / "sized.img" };
  try {
    MappedFile const file{ path, ~std::size_t{ 0u } };
    FAIL() << "expected a throw";
  } catch (std::runtime_error const& error) {
    EXPECT_TRUE(Contains(error.what(), PathToString(path))) << error.what();
  }
}

TEST(MappedFile, AFailedCreateTakesTheFileItCreatedWithIt)
{
  TempPath sample;
  auto const path{ sample.Directory() / "doomed.img" };
  EXPECT_THROW((MappedFile{ path, ~std::size_t{ 0u } }), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(path))
    << "a create that failed left its file behind";
}

TEST(MappedFile, AFailedCreateLeavesAFileItDidNotCreateAlone)
{
  TempPath sample;
  auto const path{ sample.Directory() / "precious.img" };
  WriteSample(path, "the user's only copy"sv);

  EXPECT_THROW((MappedFile{ path, ~std::size_t{ 0u } }), std::runtime_error);
  ASSERT_TRUE(std::filesystem::exists(path))
    << "a failed resize deleted a file it did not create";
  EXPECT_EQ(ReadSample(path), "the user's only copy");
}

TEST(MappedFile, AnEmptyMappingIsNothingToFlushRatherThanAnError)
{
  TempPath sample;
  { std::ofstream out{ sample.Path(), std::ios::binary }; }   // zero length
  MappedFile file{ sample.Path(), MapIntent::READ_ONLY };
  EXPECT_EQ(file.size(), 0u);
  EXPECT_NO_THROW(file.Flush());                  // no mapping, no msync, no throw
  EXPECT_NO_THROW(platform::FlushMapping({ }));
  EXPECT_NO_THROW(native::SyncFile(native::NO_FILE));
}

// On posix, giving a file a huge length and touching the end has always been
// free; on NTFS the same sequence writes real zeros over the whole range unless
// the file was marked sparse first. Same behaviour on both roads, not same price.

TEST(MappedFile, ASparseFileIsAWholeFileToWriteThroughAndReadsAsZeros)
{
  // 256 MiB: larger than a filesystem would store as real zeros without showing
  constexpr std::size_t LARGE{ std::size_t{ 256u } << 20u };
  TempPath scratch;
  auto const path{ scratch.Directory() / "huge.img" };

  {
    MappedFile huge{ path, LARGE, Backing::SPARSE };
    ASSERT_EQ(huge.size(), LARGE);
    huge[0u]          = std::byte{ 0xA5 };
    huge[LARGE - 1u]  = std::byte{ 0x5A };     // the far end, without a fill
    huge.Flush();
  }

  MappedFile const check{ path, MapIntent::READ_ONLY };
  ASSERT_EQ(check.size(), LARGE);
  EXPECT_EQ(check[0u],         std::byte{ 0xA5 });
  EXPECT_EQ(check[LARGE - 1u], std::byte{ 0x5A });
  EXPECT_EQ(check[LARGE / 2u], std::byte{ 0u })   // the hole is still zeros
    << "a hole read as something other than zero";
  EXPECT_EQ(std::filesystem::file_size(path), LARGE);
}

TEST(MappedFile, AskingForSparseChangesNothingAboutTheSmallCases)
{
  // the option is a request about cost, never about content or size
  TempPath scratch;
  auto const& directory{ scratch.Directory() };

  {
    MappedFile nothing{ directory / "empty.img", 0u, Backing::SPARSE };
    EXPECT_TRUE(nothing.empty());
    EXPECT_NO_THROW(nothing.Flush());
  }
  EXPECT_TRUE(std::filesystem::exists(directory / "empty.img"));
  EXPECT_EQ(std::filesystem::file_size(directory / "empty.img"), 0u);

  auto const sized{ directory / "sized.img" };
  {
    MappedFile state{ sized, 64u, Backing::SPARSE };
    ASSERT_EQ(state.size(), 64u);
    state[63u] = std::byte{ 0xA5 };
  }
  MappedFile const check{ sized, MapIntent::READ_ONLY };
  ASSERT_EQ(check.size(), 64u);
  EXPECT_EQ(check[63u], std::byte{ 0xA5 });

  EXPECT_THROW((MappedFile{ directory / "absent" / "x.img", 4096u,
                            Backing::SPARSE }), std::runtime_error);
}


// A refused request has to be visible before the caller starts: on a volume
// with no sparse files, 8 GiB costs 8 GiB and minutes of disk. Only win32 can
// refuse, so no lane here reaches that half.

TEST(MappedFile, AskingForNothingReportsNothing)
{
  TempPath scratch;
  auto const& directory{ scratch.Directory() };

  MappedFile const sized{ directory / "default.img", 4096u };
  EXPECT_EQ(sized.BackingInForce(), Backing::HOST_DEFAULT)
    << "a caller that asked for nothing was told it got something";

  // the question is about a length being created, and none is
  MappedFile const opened{ directory / "default.img", MapIntent::READ_ONLY };
  EXPECT_EQ(opened.BackingInForce(), Backing::HOST_DEFAULT);

  MappedFile const nothing{ };
  EXPECT_EQ(nothing.BackingInForce(), Backing::HOST_DEFAULT);
}

TEST(MappedFile, TheAnswerTravelsWithTheMappingAndNotWithTheRequest)
{
  TempPath scratch;
  auto const path{ scratch.Directory() / "moved.img" };

  MappedFile source{ path, 4096u, Backing::SPARSE };
  auto const answered{ source.BackingInForce() };
  MappedFile const landed{ std::move(source) };

  EXPECT_EQ(landed.BackingInForce(), answered)
    << "the answer was left behind by the move";
  EXPECT_EQ(source.BackingInForce(), Backing::HOST_DEFAULT)
    << "a moved-from mapping still claimed a backing";
}

// It swaps, where FileWriter discards then takes: a swapped-from MappedFile
// truthfully owns what it now holds, which Committed() and Path() could not.
TEST(MappedFile, MoveAssignmentTakesTheOtherMappingAndHandsOverItsOwn)
{
  TempPath four, six;
  WriteSample(four.Path(), "aaaa"sv);
  WriteSample(six.Path(),  "bbbbbb"sv);

  MappedFile held { four.Path(), MapIntent::READ_ONLY };
  MappedFile other{ six.Path(),  MapIntent::READ_ONLY };
  ASSERT_EQ(held.size(),  4u);
  ASSERT_EQ(other.size(), 6u);

  held = std::move(other);

  EXPECT_EQ(held.size(), 6u);
  EXPECT_EQ(ByteAt(held, 0u), 'b');
  EXPECT_EQ(other.size(), 4u) << "the mapping that was assigned over was "
                                 "dropped instead of handed across";
  EXPECT_EQ(ByteAt(other, 0u), 'a');

  EXPECT_EQ(ByteAt(held, 5u),  'b');
  EXPECT_EQ(ByteAt(other, 3u), 'a');
}
