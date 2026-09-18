#include "oxbox/utilities/span.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <span>
#include <vector>

namespace
{
  using namespace oxbox::utilities;

  TEST(SafeSubspan, WithinBounds)
  {
    std::array<int, 5> const values{ 1, 2, 3, 4, 5 };
    auto const tail{ SafeSubspan(std::span{ values }, 2u) };
    EXPECT_EQ(tail.size(), 3u);
    EXPECT_EQ(tail.front(), 3);
  }

  TEST(SafeSubspan, AtEndIsEmpty)
  {
    std::array<int, 5> const values{ };
    EXPECT_TRUE(SafeSubspan(std::span{ values }, 5u).empty());
  }

  TEST(SafeSubspan, PastEndIsEmpty)
  {
    std::array<int, 5> const values{ };
    EXPECT_TRUE(SafeSubspan(std::span{ values }, 99u).empty());
  }

  // ── Byte views (merged from the former bytes.test.cpp) ──
  struct Pod
  {
    std::uint32_t a;
    std::uint16_t b;
  };

  TEST(AsBytes, Object)
  {
    std::uint32_t const value{ 0xAABBCCDDu };
    EXPECT_EQ(AsBytes(value).size(), sizeof(value));
  }

  TEST(AsBytes, CArray)
  {
    std::uint16_t const array[4]{ 1u, 2u, 3u, 4u };
    EXPECT_EQ(AsBytes(array).size(), sizeof(array));
  }

  TEST(AsBytes, Span)
  {
    std::array<std::byte, 3> const buffer{ };
    EXPECT_EQ(AsBytes(std::span{ buffer }).size(), 3u);
  }

  TEST(AsWritableBytes, Object)
  {
    std::uint32_t value{ 0u };
    auto const writable{ AsWritableBytes(value) };
    EXPECT_EQ(writable.size(), sizeof(value));
    for (auto& byte : writable) byte = std::byte{ 0xFFu };
    EXPECT_EQ(value, 0xFFFFFFFFu);
  }

  TEST(AsWritableBytes, CArray)
  {
    std::uint16_t array[2]{ };
    EXPECT_EQ(AsWritableBytes(array).size(), sizeof(array));
  }

  TEST(AsWritableBytes, Span)
  {
    std::array<std::byte, 5> buffer{ };
    EXPECT_EQ(AsWritableBytes(std::span{ buffer }).size(), 5u);
  }


}
