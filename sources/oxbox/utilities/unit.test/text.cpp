#include "oxbox/utilities/text.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

using namespace oxbox;

TEST(Lowered, FoldsAsciiAndLeavesEverythingElseAlone)
{
  EXPECT_EQ(utilities::Lowered("Content-LENGTH"), "content-length");
  EXPECT_EQ(utilities::Lowered("already-lower"), "already-lower");
  // bytes above ASCII are not letters as far as this is concerned; a
  // UTF-8 sequence must come out byte-identical
  EXPECT_EQ(utilities::Lowered("Grüß"), std::string{ "gr\xc3\xbc\xc3\x9f" });
  EXPECT_EQ(utilities::Lowered(""), "");
}

TEST(Trimmed, CutsBothEndsAndNothingInside)
{
  EXPECT_EQ(utilities::Trimmed("  keep me  "), "keep me");
  EXPECT_EQ(utilities::Trimmed("\t\r\nvalue\r\n"), "value");
  EXPECT_EQ(utilities::Trimmed("no-padding"), "no-padding");
}

TEST(Trimmed, AllPaddingTrimsToEmptyRatherThanGarbage)
{
  EXPECT_TRUE(utilities::Trimmed(" \t\r\n").empty());
  EXPECT_TRUE(utilities::Trimmed("").empty());
}

TEST(Trimmed, HonoursACallerSuppliedCutSet)
{
  EXPECT_EQ(utilities::Trimmed("//path//", "/"), "path");
  // the default set is not consulted once the caller names one
  EXPECT_EQ(utilities::Trimmed(" x ", "/"), " x ");
}

TEST(WholeNumber, AcceptsTheWholeStringAndNothingLess)
{
  EXPECT_EQ(utilities::WholeNumber<int>("200"), 200);
  EXPECT_EQ(utilities::WholeNumber<std::size_t>("1a", 16), 26u);
  EXPECT_EQ(utilities::WholeNumber<int>("0"), 0);
}

TEST(WholeNumber, TrailingJunkIsRejectedNotTruncated)
{
  // the whole point: a prefix match here is a value silently misread
  EXPECT_FALSE(utilities::WholeNumber<int>("12abc").has_value());
  EXPECT_FALSE(utilities::WholeNumber<int>("12 ").has_value());
  EXPECT_FALSE(utilities::WholeNumber<int>("1a").has_value());  // decimal
}

TEST(WholeNumber, EmptyIsAbsentAndNotZero)
{
  EXPECT_FALSE(utilities::WholeNumber<int>("").has_value());
}
