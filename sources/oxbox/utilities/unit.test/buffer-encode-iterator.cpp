#include "oxbox/utilities/buffer-encode-iterator.hpp"

#include "oxbox/utilities/unit.test/buffer-walk.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <iterator>
#include <span>
#include <vector>

using namespace oxbox::utilities;
using oxbox::utilities::test::Walk;

namespace
{
  template <std::size_t N>
  auto Lay(WritableBytes& target, std::initializer_list<char32_t> codepoints,
           Encoding encoding, std::endian order,
           std::array<std::byte, N> const& room) -> std::vector<std::uint32_t>
  {
    BufferEncodeIterator sink{ target, encoding, order };
    for (auto const codepoint : codepoints) { *sink = codepoint; ++sink; }
    auto const written{ N - target.size() };
    auto laid{ std::vector<std::uint32_t>{ } };
    for (auto const octet : std::span{ room }.first(written))
      { laid.push_back(std::to_integer<std::uint32_t>(octet)); }
    return laid;
  }
}

TEST(BufferEncodeIterator, LaysEveryUtf8WidthDownInTheOrderItWasHanded)
{
  std::array<std::byte, 16u> room{ };
  auto target{ AsWritableBytes(room) };

  EXPECT_EQ(Lay(target, { U'A', U'é', U'€', U'\U0001F600' },
                Encoding::UTF8, std::endian::native, room),
            (std::vector<std::uint32_t>{ 0x41u,
                                         0xC3u, 0xA9u,
                                         0xE2u, 0x82u, 0xACu,
                                         0xF0u, 0x9Fu, 0x98u, 0x80u }));
  EXPECT_EQ(target.size(), 6u);
}

TEST(BufferEncodeIterator, TheByteOrderAndEncodingItWasAskedForAreHonoured)
{
  std::array<std::byte, 16u> big{ };
  auto big_target{ AsWritableBytes(big) };
  EXPECT_EQ(Lay(big_target, { U'é', U'\U0001F600' },
                Encoding::UTF16, std::endian::big, big),
            (std::vector<std::uint32_t>{ 0x00u, 0xE9u,
                                         0xD8u, 0x3Du, 0xDEu, 0x00u }));

  std::array<std::byte, 16u> little{ };
  auto little_target{ AsWritableBytes(little) };
  EXPECT_EQ(Lay(little_target, { U'é', U'\U0001F600' },
                Encoding::UTF16, std::endian::little, little),
            (std::vector<std::uint32_t>{ 0xE9u, 0x00u,
                                         0x3Du, 0xD8u, 0x00u, 0xDEu }));
}

TEST(BufferEncodeIterator, TheWriteIsTheStep)
{
  // an output iterator is single-pass and write-only, so nothing can observe
  // "encoded but not yet published"
  std::array<std::byte, 16u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  *sink = U'é';
  EXPECT_EQ(target.size(), 14u);          // two bytes, consumed by the write

  ++sink;
  EXPECT_EQ(target.size(), 14u);          // and the step is ceremony

  *sink = U'A';
  EXPECT_EQ(target.size(), 13u);
  ++sink;
  EXPECT_EQ(target.size(), 13u);

  auto written{ Bytes{ std::span{ room }.first(3u) } };
  EXPECT_EQ(Walk(written, Encoding::UTF8, std::endian::native),
            (std::vector<std::uint32_t>{ 0xE9u, 0x41u }));
}

TEST(BufferEncodeIterator, AnOverflowingWriteMovesNothing)
{
  std::array<std::byte, 3u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  *sink = U'é';                           // two bytes, fits
  EXPECT_EQ(target.size(), 1u);
  EXPECT_NE(sink, std::default_sentinel);
  EXPECT_EQ(sink.ShortfallBytes(), 0u);   // nothing missing yet

  *sink = U'\U0001F600';                  // four bytes, one byte of room
  EXPECT_EQ(target.size(), 1u);           // nothing consumed
  EXPECT_EQ(sink, std::default_sentinel); // and the sink says so
  EXPECT_EQ(sink.ShortfallBytes(), 3u);   // by exactly the three it wanted
}

TEST(BufferEncodeIterator, ARefusalLatchesAndTheNextWriteIsRefusedToo)
{
  // a narrower codepoint slipping in behind a refused one would leave a
  // subsequence of the input in the target, and no reader could tell
  std::array<std::byte, 3u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  *sink = U'é';                           // two of three bytes
  *sink = U'\U0001F600';                  // refused: four wanted, one left
  EXPECT_EQ(sink.ShortfallBytes(), 3u);

  *sink = U'A';                           // fits the leftover byte -- refused
  EXPECT_EQ(target.size(), 1u);           // the span did not move
  EXPECT_EQ(room[2u], std::byte{ 0x00u });  // the byte is still untouched
  EXPECT_EQ(sink.ShortfallBytes(), 4u);   // three, plus the one just dropped

  auto written{ Bytes{ std::span{ room }.first(2u) } };
  EXPECT_EQ(Walk(written, Encoding::UTF8, std::endian::native),
            (std::vector<std::uint32_t>{ 0xE9u }));
}

TEST(BufferEncodeIterator, ARangeCopiedIntoTooLittleRoomStopsAtTheTruncation)
{
  // ranges::copy takes its output by value and writes through its own copy,
  // so the shortfall is on the returned iterator and not the one handed in
  constexpr auto TEXT{ std::array<char32_t, 4u>{
    U'A', U'\U0001F600', U'B', U'C' } };

  std::array<std::byte, 4u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  auto const done{ std::ranges::copy(TEXT, sink) };

  EXPECT_EQ(target.size(), 3u);           // only the 'A' was ever taken
  EXPECT_EQ(done.out, std::default_sentinel);
  EXPECT_EQ(done.out.ShortfallBytes(), 3u);   // 1 for the emoji, 1 + 1 after

  auto written{ Bytes{ std::span{ room }.first(1u) } };
  EXPECT_EQ(Walk(written, Encoding::UTF8, std::endian::native),
            (std::vector<std::uint32_t>{ 0x41u }));
}

TEST(APostIncrement, CarriesTheLatchBecauseItStepsTheCallersOwnIterator)
{
  std::array<std::byte, 4u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  for (auto const codepoint : { U'A', U'\U0001F600', U'B', U'C' })
    { *sink++ = codepoint; }

  EXPECT_EQ(target.size(), 3u);           // 'B' and 'C' would have fit
  EXPECT_EQ(room[1u], std::byte{ 0x00u });
  EXPECT_EQ(sink.ShortfallBytes(), 3u);
}

TEST(BufferEncodeIterator, TheShortfallIsTheRoomItWouldHaveTakenToFinish)
{
  constexpr auto TEXT{ std::array<char32_t, 4u>{
    U'A', U'é', U'\U0001F600', U'A' } };   // 1 + 2 + 4 + 1 == 8 bytes

  std::array<std::byte, 4u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  for (auto const codepoint : TEXT) { *sink = codepoint; }
  EXPECT_EQ(sink.ShortfallBytes(), 4u);

  std::array<std::byte, 8u> resized{ };
  auto resized_target{ AsWritableBytes(resized) };
  BufferEncodeIterator retry{ resized_target, Encoding::UTF8,
                              std::endian::native };

  for (auto const codepoint : TEXT) { *retry = codepoint; }
  EXPECT_EQ(retry.ShortfallBytes(), 0u);   // and now nothing is missing
  EXPECT_TRUE(resized_target.empty());
}

TEST(BufferEncodeIterator, ABufferFilledToTheLastByteIsAtItsEndWithNothingShort)
{
  std::array<std::byte, 2u> exact{ };
  auto exact_target{ AsWritableBytes(exact) };
  BufferEncodeIterator filled{ exact_target, Encoding::UTF8,
                               std::endian::native };

  *filled = U'é';                          // two bytes into two bytes
  EXPECT_EQ(filled, std::default_sentinel);
  EXPECT_EQ(filled.ShortfallBytes(), 0u);  // at its end, and complete

  std::array<std::byte, 2u> cramped{ };
  auto cramped_target{ AsWritableBytes(cramped) };
  BufferEncodeIterator cut{ cramped_target, Encoding::UTF8,
                            std::endian::native };

  *cut = U'€';                             // three bytes into two
  EXPECT_EQ(cut, std::default_sentinel);
  EXPECT_EQ(cut.ShortfallBytes(), 1u);     // at its end, and one byte short
}

TEST(BufferEncodeIterator, ACodepointNoRoomCouldHoldIsDroppedWithoutLatching)
{
  // a refusal is not a shortfall: no buffer size fits U+20AC in a UCS1
  // unit, so the shortfall stays 0 and the sink stays open.
  std::array<std::byte, 4u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UCS1, std::endian::native };

  *sink = U'€';
  EXPECT_EQ(target.size(), 4u);            // nothing written
  EXPECT_EQ(sink.ShortfallBytes(), 0u);    // and nothing to resize by
  EXPECT_NE(sink, std::default_sentinel);  // still open

  *sink = U'A';
  EXPECT_EQ(target.size(), 3u);
  EXPECT_EQ(room[0u], std::byte{ 0x41u });
}

TEST(APostIncrement, IsAWriteFollowedByAStep)
{
  // the standard's equivalence: `*i++ = v` has the effects of `*i = v; ++i;`
  std::array<std::byte, 32u> through_post{ };
  std::array<std::byte, 32u> through_long{ };
  auto post{ AsWritableBytes(through_post) };
  auto long_{ AsWritableBytes(through_long) };
  BufferEncodeIterator by_post{ post,  Encoding::UTF8, std::endian::native };
  BufferEncodeIterator by_long{ long_, Encoding::UTF8, std::endian::native };

  for (auto const codepoint : { U'A', U'é', U'\U0001F600' }) {
    *by_post++ = codepoint;                       // the one under test
    *by_long   = codepoint;  ++by_long;           // what it must equal
  }

  EXPECT_EQ(through_post, through_long);
  EXPECT_EQ(post.size(),  long_.size());
  EXPECT_EQ(post.size(),  32u - 7u);              // 1 + 2 + 4 bytes consumed
}

TEST(APostIncrement, DiscardedIsTheSameAsPreIncrement)
{
  // std::weakly_incrementable asks that ++i and a discarded i++ agree
  std::array<std::byte, 16u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  *sink = U'A';
  EXPECT_EQ(target.size(), 15u);          // the write consumed it

  static_cast<void>(sink++);
  EXPECT_EQ(target.size(), 15u);          // discarded post-increment: nothing
  ++sink;
  EXPECT_EQ(target.size(), 15u);          // pre-increment: the same nothing
}

TEST(BufferEncodeIterator, WhatItLaysDownReadsBackThroughTheDecodeIterator)
{
  constexpr auto SAMPLES{ std::array<char32_t, 5u>{
    U'A', U'é', U'€', U'\U0001F600', U'\U0010FFFF' } };
  constexpr auto ENCODINGS{ std::array<Encoding, 3u>{
    Encoding::UTF8, Encoding::UTF16, Encoding::UCS4 } };
  constexpr auto ORDERS{ std::array<std::endian, 2u>{
    std::endian::little, std::endian::big } };

  for (auto const order : ORDERS) {
  for (auto const encoding : ENCODINGS) {
    std::array<std::byte, 64u> room{ };
    auto target{ AsWritableBytes(room) };
    BufferEncodeIterator sink{ target, encoding, order };
    for (auto const codepoint : SAMPLES) { *sink = codepoint; ++sink; }

    auto const written{ room.size() - target.size() };
    auto source{ Bytes{ std::span{ room }.first(written) } };
    auto const label{ std::format("encoding {} order {}",
                                  static_cast<int>(encoding),
                                  order == std::endian::little ? "LE" : "BE") };
    auto const wanted{ std::vector<std::uint32_t>{
      0x000041u, 0x0000E9u, 0x0020ACu, 0x01F600u, 0x10FFFFu } };
    EXPECT_EQ(Walk(source, encoding, order), wanted) << label;
    EXPECT_TRUE(source.empty()) << label << ": reader did not consume it all"; } }
}

TEST(BufferEncodeIterator, AnExactFitIsTakenAndAShortRoomIsRefusedWhole)
{
  std::array<std::byte, 4u> exact{ };
  auto exact_target{ AsWritableBytes(exact) };
  BufferEncodeIterator fits{ exact_target, Encoding::UTF8, std::endian::native };

  *fits = U'\U0001F600';
  EXPECT_TRUE(exact_target.empty());      // four bytes are exactly enough,
  EXPECT_TRUE(fits == std::default_sentinel);   // and the write consumed them
  ++fits;
  EXPECT_TRUE(exact_target.empty());      // the step adds nothing to that

  std::array<std::byte, 3u> cramped{ };
  auto cramped_target{ AsWritableBytes(cramped) };
  BufferEncodeIterator refused{ cramped_target, Encoding::UTF8,
                                std::endian::native };

  *refused = U'\U0001F600';
  EXPECT_EQ(cramped_target.size(), 3u);   // a refused write consumes nothing
  ++refused;
  EXPECT_EQ(cramped_target.size(), 3u);
  EXPECT_TRUE(refused == std::default_sentinel);   // the overflow is latched
}

TEST(BufferEncodeIterator, RoomToWriteInIsWhatKeepsItFromItsEnd)
{
  std::array<std::byte, 2u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  EXPECT_TRUE (sink != std::default_sentinel);
  EXPECT_TRUE (std::default_sentinel != sink);
  EXPECT_FALSE(sink == std::default_sentinel);

  *sink = U'A';                           // one octet of the two
  ++sink;
  EXPECT_EQ(target.size(), 1u);
  EXPECT_TRUE(sink != std::default_sentinel);   // still room for one more

  *sink = U'B';                           // and that exhausts it
  ++sink;
  EXPECT_TRUE(target.empty());
  EXPECT_TRUE (sink == std::default_sentinel);
  EXPECT_TRUE (std::default_sentinel == sink);
  EXPECT_FALSE(sink != std::default_sentinel);
}

TEST(BufferEncodeIterator, AWriteIntoAnEmptyTargetStoresNothingAndStaysAtItsEnd)
{
  std::array<std::byte, 4u> room{ };
  auto target{ AsWritableBytes(std::span{ room }.first(0u)) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  EXPECT_TRUE(sink == std::default_sentinel);

  *sink = U'A';
  EXPECT_TRUE(target.empty());
  EXPECT_EQ(room[0u], std::byte{ 0x00u });
  EXPECT_TRUE(sink == std::default_sentinel);

  ++sink;
  EXPECT_TRUE(sink == std::default_sentinel);
}
