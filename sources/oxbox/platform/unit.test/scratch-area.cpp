// "A directory nothing else is using", held to account: in the project this
// comes from, two areas asked for with one purpose named the same directory
// and the second's arrival deleted the first's files.
#include "oxbox/platform/scratch-area.hpp"

#include "oxbox/platform/unit.test/fixtures.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <string>
#include <utility>

namespace oxbox::platform
{
  namespace
  {
    namespace stdfs = std::filesystem;
    using test::WriteSample;

    TEST(ScratchArea, TwoAreasWithOnePurposeAreTwoDirectories)
    {
      ScratchArea first{ "same-purpose" };
      WriteSample(first.File("kept.bin"), "x");

      {
        ScratchArea const second{ "same-purpose" };
        EXPECT_NE(first.Path(), second.Path());
        EXPECT_TRUE(stdfs::is_directory(second.Path()));
      }

      // the first area is untouched by the second having come and gone
      EXPECT_TRUE(stdfs::exists(first.File("kept.bin")));
      EXPECT_TRUE(stdfs::is_directory(first.Path()));
    }

    TEST(ScratchArea, AnAreaTakesItsDirectoryWithItWhenItGoes)
    {
      stdfs::path remembered;
      {
        ScratchArea const area{ "goes-away" };
        remembered = area.Path();
        WriteSample(area.File("inside.bin"), "x");
        // recursively: a directory with something in it still goes
        stdfs::create_directory(remembered / "deeper");
        WriteSample(remembered / "deeper" / "further.bin", "x");
      }
      EXPECT_FALSE(stdfs::exists(remembered));
    }

    // a moved-from area removes nothing, or handing one to a caller by value
    // would delete the directory it just made
    TEST(ScratchArea, MovingAnAreaMovesItsDirectoryAndNotACopyOfIt)
    {
      ScratchArea made{ "moved" };
      auto const remembered{ made.Path() };

      {
        ScratchArea const moved{ std::move(made) };
        EXPECT_EQ(moved.Path(), remembered);
        EXPECT_TRUE(stdfs::is_directory(remembered));
      }
      EXPECT_FALSE(stdfs::exists(remembered));
    }

    // the old directory has to go with the assignment; an implementation that
    // swapped, or that removed the new one, would pass every case above
    TEST(ScratchArea, AssigningOverAnAreaRemovesTheOneItReplaced)
    {
      ScratchArea first{ "replaced" };
      ScratchArea second{ "replacement" };
      auto const old_path{ first.Path() };
      auto const new_path{ second.Path() };
      ASSERT_NE(old_path, new_path);

      first = std::move(second);
      EXPECT_FALSE(stdfs::exists(old_path))
        << "the area that was replaced kept its directory";
      EXPECT_EQ(first.Path(), new_path);
      EXPECT_TRUE(stdfs::is_directory(new_path));

      // and the moved-from one removes nothing when it goes
      {
        ScratchArea const emptied{ std::move(second) };
        static_cast<void>(emptied);
      }
      EXPECT_TRUE(stdfs::is_directory(new_path));
    }

    TEST(ScratchArea, TheNameSaysWhoMadeItAndWhatItIsFor)
    {
      ScratchArea const defaulted{ "a-purpose" };
      auto const name{ defaulted.Path().filename().string() };
      EXPECT_TRUE(name.starts_with("oxbox-")) << name;
      EXPECT_TRUE(name.ends_with("-a-purpose")) << name;

      ScratchArea const named{ "a-purpose", "consumer" };
      auto const other{ named.Path().filename().string() };
      EXPECT_TRUE(other.starts_with("consumer-")) << other;

      // the token is one fact about this process, so both names carry it
      auto const token{ std::format("{:016x}", ScratchToken()) };
      EXPECT_NE(name.find(token), std::string::npos) << name;
      EXPECT_NE(other.find(token), std::string::npos) << other;
    }

    TEST(ScratchArea, FileIsAPathInsideItThatNothingHasCreated)
    {
      ScratchArea const area{ "files" };
      auto const inside{ area.File("document.bin") };
      EXPECT_EQ(inside.parent_path(), area.Path());
      EXPECT_FALSE(stdfs::exists(inside));
      WriteSample(inside, "content");
      EXPECT_TRUE(stdfs::exists(inside));
    }
  }
}
