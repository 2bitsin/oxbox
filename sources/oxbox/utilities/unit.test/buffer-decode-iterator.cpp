#include "oxbox/utilities/buffer-decode-iterator.hpp"

#include "oxbox/utilities/unit.test/buffer-walk.hpp"
#include "oxbox/utilities/unit.test/octets.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iterator>
#include <vector>

using namespace oxbox::utilities;
using oxbox::utilities::test::Octets;
using oxbox::utilities::test::Walk;

TEST(BufferDecodeIterator, WalksAUtf8BufferOneCodepointAtATime)
{
  constexpr auto WIRE{ Octets(0x41u,                          // U+0041
                              0xC3u, 0xA9u,                   // U+00E9
                              0xF0u, 0x9Fu, 0x98u, 0x80u) };  // U+1F600
  auto source{ AsBytes(WIRE) };

  EXPECT_EQ(Walk(source, Encoding::UTF8, std::endian::native),
            (std::vector<std::uint32_t>{ 0x41u, 0xE9u, 0x01F600u }));
}

TEST(BufferDecodeIterator, TheSpanItWasHandedIsConsumedAsItWalks)
{
  constexpr auto WIRE{ Octets(0x41u, 0xC3u, 0xA9u) };
  auto source{ AsBytes(WIRE) };
  ASSERT_EQ(source.size(), 3u);

  using Sentinel = BufferDecodeIterator::sentinel;
  BufferDecodeIterator it{ source, Encoding::UTF8, std::endian::native };
  EXPECT_EQ(std::uint32_t{ *it }, 0x41u);
  EXPECT_EQ(source.size(), 2u);          // the first octet is already spent

  ++it;
  EXPECT_EQ(std::uint32_t{ *it }, 0xE9u);
  EXPECT_TRUE(source.empty());

  ++it;
  EXPECT_TRUE(it == Sentinel{ });
}

TEST(BufferDecodeIterator, AnEmptyBufferIsExhaustedBeforeTheFirstStep)
{
  Bytes source{ };
  BufferDecodeIterator it{ source, Encoding::UTF8, std::endian::native };
  EXPECT_TRUE(it == BufferDecodeIterator::sentinel{ });
  EXPECT_TRUE(Walk(source, Encoding::UTF8, std::endian::native).empty());
}

TEST(BufferDecodeIterator, TheDefaultArgumentsAreUtf8InNativeOrder)
{
  constexpr auto WIRE{ Octets(0xC3u, 0xA9u) };
  auto source{ AsBytes(WIRE) };

  BufferDecodeIterator it{ source };
  ASSERT_TRUE(it != BufferDecodeIterator::sentinel{ });
  EXPECT_EQ(std::uint32_t{ *it }, 0xE9u);
}

TEST(BufferDecodeIterator, WalksTheFixedWidthEncodingsInTheOrderAsked)
{
  constexpr auto ASCII{ Octets(0x41u, 0xE9u, 0xFFu) };
  auto ascii{ AsBytes(ASCII) };
  EXPECT_EQ(Walk(ascii, Encoding::ASCII, std::endian::native),
            (std::vector<std::uint32_t>{ 0x41u, 0xE9u, 0xFFu }));

  constexpr auto UCS2{ Octets(0x00u, 0xE9u, 0x00u, 0x41u) };
  auto ucs2{ AsBytes(UCS2) };
  EXPECT_EQ(Walk(ucs2, Encoding::UCS2, std::endian::big),
            (std::vector<std::uint32_t>{ 0xE9u, 0x41u }));

  constexpr auto UCS4{ Octets(0x00u, 0xF6u, 0x01u, 0x00u,
                              0x41u, 0x00u, 0x00u, 0x00u) };
  auto ucs4{ AsBytes(UCS4) };
  EXPECT_EQ(Walk(ucs4, Encoding::UCS4, std::endian::little),
            (std::vector<std::uint32_t>{ 0x01F600u, 0x41u }));
}

TEST(BufferDecodeIterator, WalksAUtf16BufferInEitherByteOrder)
{
  constexpr auto LITTLE{ Octets(0xE9u, 0x00u,                    // U+00E9
                                0x3Du, 0xD8u, 0x00u, 0xDEu) };   // U+1F600
  constexpr auto BIG   { Octets(0x00u, 0xE9u,
                                0xD8u, 0x3Du, 0xDEu, 0x00u) };
  auto const WANTED{ std::vector<std::uint32_t>{ 0xE9u, 0x01F600u } };

  auto little{ AsBytes(LITTLE) };
  EXPECT_EQ(Walk(little, Encoding::UTF16, std::endian::little), WANTED);

  auto big{ AsBytes(BIG) };
  EXPECT_EQ(Walk(big, Encoding::UTF16, std::endian::big), WANTED);
}

TEST(BufferDecodeIterator, ATruncatedTailEndsTheWalkAndIsLeftInTheSpan)
{
  constexpr auto WIRE{ Octets(0x41u, 0xE2u, 0x82u) };   // 'A' then a cut U+20AC
  auto source{ AsBytes(WIRE) };

  EXPECT_EQ(Walk(source, Encoding::UTF8, std::endian::native),
            (std::vector<std::uint32_t>{ 0x41u }));
  EXPECT_EQ(source.size(), 2u);   // the incomplete sequence is still there
}

TEST(BufferDecodeIterator, AnUndecodableOctetStopsTheWalk)
{
  constexpr auto WIRE{ Octets(0x41u, 0xFFu, 0x42u) };
  auto source{ AsBytes(WIRE) };

  EXPECT_EQ(Walk(source, Encoding::UTF8, std::endian::native),
            (std::vector<std::uint32_t>{ 0x41u }));
  EXPECT_EQ(source.size(), 2u);
}

TEST(BufferDecodeIterator, ADefaultConstructedIteratorIsItsOwnEnd)
{
  BufferDecodeIterator it{ };
  EXPECT_TRUE (it == std::default_sentinel);
  EXPECT_TRUE (std::default_sentinel == it);
  EXPECT_FALSE(it != std::default_sentinel);
  EXPECT_FALSE(std::default_sentinel != it);

  ++it;                                   // and the step keeps it there
  EXPECT_TRUE(it == std::default_sentinel);
}

TEST(BufferDecodeIterator, BothOrdersOfTheComparisonAgree)
{
  constexpr auto WIRE{ Octets(0x41u) };
  auto source{ AsBytes(WIRE) };

  BufferDecodeIterator it{ source };
  EXPECT_TRUE (it != std::default_sentinel);
  EXPECT_TRUE (std::default_sentinel != it);
  EXPECT_FALSE(std::default_sentinel == it);

  ++it;
  EXPECT_TRUE (it == std::default_sentinel);
  EXPECT_TRUE (std::default_sentinel == it);
  EXPECT_FALSE(std::default_sentinel != it);
}

TEST(BufferDecodeIterator, AMalformedSequenceNeverSurfacesAsTheInvalidCodepoint)
{
  constexpr auto WIRE{ Octets(0x41u,
                              0xEDu, 0xA0u, 0x80u,   // CESU-8 surrogate
                              0x42u) };
  auto source{ AsBytes(WIRE) };

  auto const spelled{ Walk(source, Encoding::UTF8, std::endian::native) };
  EXPECT_EQ(spelled, (std::vector<std::uint32_t>{ 0x41u }));
  EXPECT_EQ(std::ranges::count(spelled,
              std::uint32_t{ INVALID_CODEPOINT<char32_t> }), 0);

  // the four octets it could not make a codepoint of are still the caller's
  EXPECT_EQ(source.size(), 4u);
}
