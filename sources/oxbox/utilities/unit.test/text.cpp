#include <ranges>

// Reject the unavailable macOS adaptor even when the host library provides it.
#define join_with OXBOX_JOIN_WITH_IS_NOT_PORTABLE
#include "oxbox/utilities/text.hpp"
#undef join_with

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <sstream>
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
  EXPECT_EQ(utilities::Trimmed("\v\fvalue\f\v"), "value");
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

TEST(Joined, EmptySingleAndSeveralStrings)
{
  EXPECT_EQ(utilities::Joined(std::array<std::string_view, 0>{ }, ", "), "");
  EXPECT_EQ(utilities::Joined(std::array{ "one" }, ", "), "one");
  EXPECT_EQ(utilities::Joined(std::array{ "one", "two", "three" }, ", "), "one, two, three");
  EXPECT_EQ(utilities::Joined(std::array{ "", "", "" }, ","), ",,");
  EXPECT_EQ(utilities::Joined(std::array{ "", "middle", "" }, ","), ",middle,");
  EXPECT_EQ(utilities::Joined(std::array{ "a", "b" }, ""), "ab");
}

TEST(Joined, FormatsValuesAndProjectsMembers)
{
  EXPECT_EQ(utilities::Joined(std::array{ 1, 20, -3 }, "/"), "1/20/-3");
  auto const values{ std::array{ std::pair{ "one", 1 }, std::pair{ "two", 2 } } };
  EXPECT_EQ(utilities::Joined(values, ",", &decltype(values)::value_type::first), "one,two");
  EXPECT_EQ(utilities::Joined(values, ",", &decltype(values)::value_type::second), "1,2");
}

TEST(Joined, OwnsTemporaryProjectionStringsAndAcceptsCharRanges)
{
  auto const render{ [](int value) { return std::string(40, static_cast<char>('a' + value)); } };
  EXPECT_EQ(utilities::Joined(std::array{ 0, 1 }, "/", render), render(0) + "/" + render(1));
  auto const chars{ std::array{ std::array{ 'a', 'b' }, std::array{ 'c', 'd' } } };
  EXPECT_EQ(utilities::Joined(chars, ":"), "ab:cd");
  char const raw[][2]{ { 'a', 'b' }, { 'c', 'd' } };
  EXPECT_EQ(utilities::Joined(raw, ":"), "ab:cd");
  EXPECT_EQ(utilities::Joined(std::array{ std::string{ "a\0b", 3 }, std::string{ "c" } }, ":"),
            (std::string{ "a\0b:c", 5 }));
}

TEST(Joined, ConsumesAnInputRangeOnce)
{
  std::istringstream input{ "1 2 3" };
  EXPECT_EQ(utilities::Joined(std::ranges::istream_view<int>(input), ","), "1,2,3");
}

static_assert(utilities::Joined(std::array{ "a", "b" }, ":") == "a:b");
static_assert(utilities::Joined(std::array{ std::string_view{ "a" }, std::string_view{ "b" } },
                               ":", std::identity{ }) == "a:b");

namespace
{
  struct StringLike
  {
    std::string value;
    operator std::string_view() const { return value; }
  };
}

TEST(Joined, KeepsAProjectedStringLikeOwnerAlive)
{
  auto const render{ [](int) { return StringLike{ std::string(80, 'x') }; } };
  EXPECT_EQ(utilities::Joined(std::array{ 1, 2 }, ":", render),
            std::string(80, 'x') + ":" + std::string(80, 'x'));
}

TEST(Joined, BorrowsAMoveOnlyViewForTheCall)
{
  auto owned{ std::views::all(std::array{ std::string{ "one" }, std::string{ "two" } }) };
  EXPECT_EQ(utilities::Joined(owned, "/"), "one/two");
  EXPECT_EQ(utilities::Joined(std::move(owned), "/"), "one/two");
  auto pieces{ std::array{ std::views::all(std::string{ "a" }),
                          std::views::all(std::string{ "b" }) } };
  EXPECT_EQ(utilities::Joined(pieces, "/"), "a/b");
}
