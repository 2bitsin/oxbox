// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests
#include "oxbox/utilities/serdes.hpp"
#include "oxbox/utilities/span.hpp"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace
{
  using namespace oxbox::utilities;
}


TEST(StoreFetch, MirrorRoundTrip)
{
  std::array<std::byte, sizeof(U32)> buf{ };
  Store<U32>(0xDEADBEEFu, std::span{ buf }, NoAdvance);
  EXPECT_EQ((Fetch<U32>(std::span<std::byte const>{ buf }, NoAdvance)), 0xDEADBEEFu);
}

TEST(StoreFetch, StoreWritesBigEndianByteOrder)
{
  std::array<std::byte, 4> buf{ };
  Store<U32, std::endian::big>(0x11223344u, std::span{ buf }, NoAdvance);
  EXPECT_EQ(buf, (std::array<std::byte, 4>{ std::byte{ 0x11u }, std::byte{ 0x22u },
                                            std::byte{ 0x33u }, std::byte{ 0x44u } }));
}

TEST(StoreFetch, AdvancingFormsConsumeTheCursor)
{
  std::array<std::byte, 12> storage{ };
  WritableBytes writer{ storage };
  Store<U32>(0x11111111u, writer);
  Store<U32>(0x22222222u, writer);
  EXPECT_EQ(writer.size(), 4u);

  Bytes reader{ storage };
  EXPECT_EQ(Fetch<U32>(reader), 0x11111111u);
  EXPECT_EQ(Fetch<U32>(reader), 0x22222222u);
  EXPECT_EQ(reader.size(), 4u);
}

// on byte spans the round trip is constant-evaluable end to end
static_assert([] { std::array<std::byte, 4> buf{ };
                   Store<U32>(0xDEADBEEFu, std::span{ buf }, NoAdvance);
                   return Fetch<U32>(std::span<std::byte const, 4>{ buf }, NoAdvance)
                       == 0xDEADBEEFu; }());

// ---- floating point, the same road ------------------------------------

TEST(StoreFetch, CarriesFloatingPointThroughEitherByteOrder)
{
  std::array<std::byte, sizeof(double)> buf{ };
  Store<double>(0.5, std::span{ buf }, NoAdvance);
  EXPECT_EQ((Fetch<double>(std::span<std::byte const>{ buf }, NoAdvance)), 0.5);

  Store<double, std::endian::big>(-2.75, std::span{ buf }, NoAdvance);
  EXPECT_EQ((Fetch<double, std::endian::big>(
              std::span<std::byte const>{ buf }, NoAdvance)), -2.75);

  std::array<std::byte, sizeof(float)> narrow{ };
  Store<float, std::endian::big>(1.5F, std::span{ narrow }, NoAdvance);
  EXPECT_EQ((Fetch<float, std::endian::big>(
              std::span<std::byte const>{ narrow }, NoAdvance)), 1.5F);
}

TEST(StoreFetch, AByteSwappedDoubleIsActuallyReversed)
{
  // the property the round trip alone cannot see: both directions could be
  // wrong the same way
  std::array<std::byte, 8u> big{ };
  Store<double, std::endian::big>(0.5, std::span{ big }, NoAdvance);
  std::array<std::byte, 8u> little{ };
  Store<double, std::endian::little>(0.5, std::span{ little }, NoAdvance);
  std::ranges::reverse(little);
  EXPECT_EQ(big, little);
}

// ---- the bounded pair -------------------------------------------------

TEST(BoundedReader, ReadsAWholeFrameAndStaysSound)
{
  constexpr std::array<std::byte, 8u> FRAME{
    std::byte{ 0x11u }, std::byte{ 0x22u }, std::byte{ 0x33u }, std::byte{ 0x44u },
    std::byte{ 0xAAu }, std::byte{ 0xBBu }, std::byte{ 0xCCu }, std::byte{ 0xDDu } };
  BoundedReader reader{ Bytes{ FRAME } };
  EXPECT_EQ((reader.Fetch<U32, std::endian::big>()), 0x11223344u);
  EXPECT_EQ(reader.Take(2u).size(), 2u);
  EXPECT_EQ(reader.Remaining(), 2u);
  EXPECT_EQ(reader.At(), 6u);
  EXPECT_TRUE(reader.Sound());
}

TEST(BoundedReader, EveryTruncationIsRefusedAndNoneOverruns)
{
  // a frame that stops early has to read like one at every length
  constexpr std::array<std::byte, 8u> FRAME{
    std::byte{ 1u }, std::byte{ 2u }, std::byte{ 3u }, std::byte{ 4u },
    std::byte{ 5u }, std::byte{ 6u }, std::byte{ 7u }, std::byte{ 8u } };
  for (std::size_t length{ 0u }; length < FRAME.size(); ++length)
  {
    BoundedReader reader{ Bytes{ FRAME }.first(length) };
    static_cast<void>(reader.Fetch<U32>());
    static_cast<void>(reader.Fetch<U32>());
    EXPECT_FALSE(reader.Sound()) << "a frame cut to " << length << " was believed";
  }
  BoundedReader whole{ Bytes{ FRAME } };
  static_cast<void>(whole.Fetch<U32>());
  static_cast<void>(whole.Fetch<U32>());
  EXPECT_TRUE(whole.Sound());
}

TEST(BoundedReader, AnUnsoundReaderStaysUnsoundAndAnswersWithDefaults)
{
  constexpr std::array<std::byte, 2u> SHORT{ std::byte{ 0xFFu }, std::byte{ 0xFFu } };
  BoundedReader reader{ Bytes{ SHORT } };
  EXPECT_EQ(reader.Fetch<U32>(), 0u);
  EXPECT_FALSE(reader.Sound());
  EXPECT_EQ(reader.Remaining(), 0u);
  // the two bytes still there must not come back: one failure is the whole read
  EXPECT_EQ(reader.Fetch<U08>(), 0u);
  EXPECT_TRUE(reader.Take(1u).empty());
  EXPECT_TRUE(reader.Text(1u).empty());
  EXPECT_FALSE(reader.Sound());
}

TEST(BoundedReader, ALayoutCanLatchTheFlagItself)
{
  constexpr std::array<std::byte, 4u> FRAME{
    std::byte{ 0xDEu }, std::byte{ 0xADu }, std::byte{ 0xBEu }, std::byte{ 0xEFu } };
  BoundedReader reader{ Bytes{ FRAME } };

  auto const magic{ reader.Fetch<U32, std::endian::big>() };
  EXPECT_TRUE(reader.Sound()) << "the bytes were all there";
  if (magic != 0x6F786230u)          // not a frame this build wrote
    reader.Refuse();

  EXPECT_FALSE(reader.Sound());
  // sticky the same way an overrun is, including for bytes that are there
  EXPECT_TRUE(reader.Take(0u).empty());
  EXPECT_EQ(reader.Remaining(), 0u);
  BoundedReader again{ Bytes{ FRAME } };
  again.Refuse();
  EXPECT_EQ(again.Fetch<U08>(), 0u);
  EXPECT_TRUE(again.Text(1u).empty());
  EXPECT_FALSE(again.Sound());
}

TEST(BoundedWriter, ALayoutCanLatchTheFlagItselfToo)
{
  // Whole() at the bottom is still the one question
  std::array<std::byte, 8u> into{ };
  BoundedWriter writer{ WritableBytes{ into } };
  writer.Store<U32>(1u);
  EXPECT_TRUE(writer.Sound());
  writer.Refuse();
  EXPECT_FALSE(writer.Sound());
  EXPECT_FALSE(writer.Whole());
  writer.Store<U32>(2u);             // would have fitted
  EXPECT_EQ(into[4], std::byte{ 0u });
  EXPECT_FALSE(writer.Sound());
}

TEST(BoundedReader, ALengthThatWouldWrapIsRefusedRatherThanAdded)
{
  // `at + count` overflows and lets a 2^64-2 payload look like a short one
  constexpr std::array<std::byte, 4u> FRAME{
    std::byte{ 0u }, std::byte{ 1u }, std::byte{ 2u }, std::byte{ 3u } };
  BoundedReader reader{ Bytes{ FRAME } };
  static_cast<void>(reader.Take(2u));
  EXPECT_TRUE(reader.Take(std::numeric_limits<std::size_t>::max() - 1u).empty());
  EXPECT_FALSE(reader.Sound());
}

TEST(BoundedReader, TextIsAViewIntoTheSourceAndAlignSkipsPadding)
{
  constexpr std::array<std::byte, 8u> FRAME{
    std::byte{ 'o' }, std::byte{ 'x' }, std::byte{ 0u }, std::byte{ 0u },
    std::byte{ 0u }, std::byte{ 0u }, std::byte{ 0u }, std::byte{ 0xFEu } };
  BoundedReader reader{ Bytes{ FRAME } };
  auto const text{ reader.Text(2u) };
  EXPECT_EQ(text, "ox");
  EXPECT_EQ(static_cast<void const*>(text.data()),
            static_cast<void const*>(FRAME.data()));
  reader.Align(8u);
  EXPECT_EQ(reader.At(), 8u);
  EXPECT_TRUE(reader.Sound());
  EXPECT_EQ(reader.Remaining(), 0u);
}

TEST(BoundedWriter, FillsABufferSizedInAdvanceAndSaysWhenItIsWhole)
{
  std::array<std::byte, 12u> into{ };
  BoundedWriter writer{ WritableBytes{ into } };
  writer.Store<U32, std::endian::big>(0x11223344u);
  writer.Text("ox");
  writer.Align(8u);
  writer.Store<U32>(0u);
  EXPECT_TRUE(writer.Sound());
  EXPECT_TRUE(writer.Whole());
  EXPECT_EQ(into[0], std::byte{ 0x11u });
  EXPECT_EQ(into[4], std::byte{ 'o' });
  // the padding is written, not skipped
  EXPECT_EQ(into[6], std::byte{ 0u });
  EXPECT_EQ(into[7], std::byte{ 0u });
}

TEST(BoundedWriter, AStepThatWillNotFitWritesNothingAndLatches)
{
  std::array<std::byte, 6u> into{ };
  std::ranges::fill(into, std::byte{ 0xEEu });
  BoundedWriter writer{ WritableBytes{ into } };
  writer.Store<U32>(0x11111111u);
  writer.Store<U32>(0x22222222u);          // two bytes short
  EXPECT_FALSE(writer.Sound());
  EXPECT_FALSE(writer.Whole());
  // never a partial scalar: the two bytes that would have fitted are untouched
  EXPECT_EQ(into[4], std::byte{ 0xEEu });
  EXPECT_EQ(into[5], std::byte{ 0xEEu });
  writer.Put(Bytes{ });
  writer.Text("");
  EXPECT_FALSE(writer.Sound());
}

// Skipping padding that is not there is fine; writing padding that will not
// fit is not.
TEST(BoundedReader, AligningPastTheEndIsAnOverrunLikeAnyOther)
{
  constexpr std::array<std::byte, 4u> FRAME{
    std::byte{ 1u }, std::byte{ 2u }, std::byte{ 3u }, std::byte{ 4u } };

  // exactly at the end and already aligned: nothing to skip, still sound
  BoundedReader flush{ Bytes{ FRAME } };
  static_cast<void>(flush.Take(4u));
  flush.Align(4u);
  EXPECT_TRUE(flush.Sound());
  EXPECT_EQ(flush.At(), 4u);

  // at the end but not aligned: the padding it would skip is not there
  BoundedReader ragged{ Bytes{ FRAME } };
  static_cast<void>(ragged.Take(3u));
  ragged.Align(8u);
  EXPECT_FALSE(ragged.Sound());
}

TEST(BoundedReader, TakingNothingAtTheExactEndStaysSound)
{
  // an empty span is what was asked for, and asking for it is not an overrun
  constexpr std::array<std::byte, 2u> FRAME{ std::byte{ 1u }, std::byte{ 2u } };
  BoundedReader reader{ Bytes{ FRAME } };
  static_cast<void>(reader.Take(2u));
  EXPECT_TRUE(reader.Take(0u).empty());
  EXPECT_TRUE(reader.Sound());
  EXPECT_EQ(reader.Remaining(), 0u);
  EXPECT_TRUE(reader.Text(0u).empty());
  EXPECT_TRUE(reader.Sound());
}

TEST(BoundedWriter, AligningPastTheEndLatchesBecauseThePaddingIsWritten)
{
  std::array<std::byte, 4u> into{ };
  std::ranges::fill(into, std::byte{ 0xEEu });
  BoundedWriter writer{ WritableBytes{ into } };
  writer.Store<U08>(1u);
  writer.Align(8u);                  // seven bytes of padding into three
  EXPECT_FALSE(writer.Sound());
  // it wrote none of it, because a partial claim is not a claim
  EXPECT_EQ(into[1], std::byte{ 0xEEu });
  EXPECT_EQ(into[3], std::byte{ 0xEEu });

  // already aligned at the end: nothing to pad, still sound and whole
  std::array<std::byte, 4u> exact{ };
  BoundedWriter flush{ WritableBytes{ exact } };
  flush.Store<U32>(0u);
  flush.Align(4u);
  EXPECT_TRUE(flush.Sound());
  EXPECT_TRUE(flush.Whole());
}

// A float is carried as bits, so the values with no equality of their own are
// what says the swap is a swap and not an arithmetic round trip.
TEST(StoreFetch, CarriesNaNPayloadAndNegativeZeroThroughAByteSwap)
{
  auto const bits = [](double value) { return std::bit_cast<U64>(value); };

  // a payload nothing would reconstruct by arithmetic
  auto const nan{ std::bit_cast<double>(U64{ 0x7FF8'0000'DEAD'BEEFu }) };
  std::array<std::byte, 8u> buf{ };
  Store<double, std::endian::big>(nan, std::span{ buf }, NoAdvance);
  auto const back{ Fetch<double, std::endian::big>(
                     std::span<std::byte const>{ buf }, NoAdvance) };
  EXPECT_TRUE(std::isnan(back));
  EXPECT_EQ(bits(back), bits(nan)) << "the payload has to survive, bit for bit";

  // -0.0 compares equal to 0.0, so only the sign bit catches a dropped swap
  Store<double, std::endian::big>(-0.0, std::span{ buf }, NoAdvance);
  auto const zero{ Fetch<double, std::endian::big>(
                     std::span<std::byte const>{ buf }, NoAdvance) };
  EXPECT_EQ(zero, 0.0);
  EXPECT_TRUE(std::signbit(zero));
  EXPECT_EQ(bits(zero), bits(-0.0));

  // in its own buffers, because `buf` is holding -0.0 by now
  std::array<std::byte, 8u> big_nan{ };
  std::array<std::byte, 8u> little_nan{ };
  Store<double, std::endian::big>(nan, std::span{ big_nan }, NoAdvance);
  Store<double, std::endian::little>(nan, std::span{ little_nan }, NoAdvance);
  std::ranges::reverse(little_nan);
  EXPECT_EQ(big_nan, little_nan);
}

TEST(BoundedRoundTrip, WhatTheWriterLaidDownIsWhatTheReaderTakesBack)
{
  std::array<std::byte, 24u> frame{ };   // 4 + 5, padded to 8, then 8
  BoundedWriter writer{ WritableBytes{ frame } };
  writer.Store<U32, std::endian::big>(0xDEADBEEFu);
  writer.Text("oxbox");
  writer.Align(8u);
  writer.Store<double, std::endian::big>(0.5);
  ASSERT_TRUE(writer.Whole());

  BoundedReader reader{ Bytes{ frame } };
  EXPECT_EQ((reader.Fetch<U32, std::endian::big>()), 0xDEADBEEFu);
  EXPECT_EQ(reader.Text(5u), "oxbox");
  reader.Align(8u);
  EXPECT_EQ((reader.Fetch<double, std::endian::big>()), 0.5);
  EXPECT_TRUE(reader.Sound());
  EXPECT_EQ(reader.Remaining(), 0u);
}

// the whole bounded read is constant-evaluable
static_assert([] {
  std::array<std::byte, 4u> frame{ };
  BoundedWriter writer{ WritableBytes{ frame } };
  writer.Store<U32, std::endian::big>(0x01020304u);
  if (!writer.Whole()) return false;
  BoundedReader reader{ Bytes{ frame } };
  return reader.Fetch<U32, std::endian::big>() == 0x01020304u
      && reader.Sound() && reader.Remaining() == 0u;
}());
// NOLINTEND(misc-non-private-member-variables-in-classes)
