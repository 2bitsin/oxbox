#include "oxbox/utilities/fixed-string.hpp"

#include <gtest/gtest.h>

#include <string_view>

namespace
{
  using oxbox::utilities::FixedString;

  TEST(FixedString, DeducesSizeFromLiteral)
  {
    constexpr FixedString fs{ "hello" };
    static_assert(sizeof(fs.data) == 6u);  // 5 chars + NUL
    EXPECT_EQ(fs.size(), 6u);              // size() is the CAPACITY
    EXPECT_EQ(fs.view(), std::string_view{ "hello" });
  }

  TEST(FixedString, EmptyLiteral)
  {
    constexpr FixedString fs{ "" };
    EXPECT_EQ(fs.size(), 1u);              // just the NUL
    EXPECT_TRUE(fs.view().empty());
  }

  TEST(FixedString, ChainedAccess)
  {
    constexpr FixedString fs{ "abc" };
    EXPECT_EQ(fs.data[0], 'a');
    EXPECT_EQ(fs.data[1], 'b');
    EXPECT_EQ(fs.data[2], 'c');
    EXPECT_EQ(fs.data[3], '\0');
  }

  TEST(FixedString, UsableAsNTTP)
  {
    auto echo = []<FixedString S>() -> std::string_view { return S.view(); };
    EXPECT_EQ(echo.operator()<"hello">(), std::string_view{ "hello" });
    EXPECT_EQ(echo.operator()<"world">(), std::string_view{ "world" });
  }

  TEST(FixedString, Equality)
  {
    constexpr FixedString a{ "abc" };
    constexpr FixedString b{ "abc" };
    constexpr FixedString c{ "abd" };
    static_assert(a == b);
    static_assert(!(a == c));
  }
}
