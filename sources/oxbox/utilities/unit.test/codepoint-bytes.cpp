#include "oxbox/utilities/codepoint-bytes.hpp"

#include "oxbox/utilities/unit.test/octets.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace oxbox::utilities;
using oxbox::utilities::test::Octets;

namespace
{
  struct Pulled
  {
    std::optional<std::uint32_t> codepoint;
    std::size_t                  consumed;
  };

  auto Pull(Bytes& source, Encoding encoding, std::endian order) -> Pulled
  {
    auto const before{ source.size() };
    auto const value { DecodeFromBytes<char32_t>(source, encoding, order) };
    return { value ? std::optional{ std::uint32_t{ *value } } : std::nullopt,
             before - source.size() };
  }

  // A byte-per-unit encoding has no byte order to state.
  auto Pull(Bytes& source, Encoding encoding) -> Pulled
  { return Pull(source, encoding, std::endian::native); }

  static_assert(Encoding::UCS1  == Encoding::ASCII);
  static_assert(Encoding::UTF32 == Encoding::UCS4 );
}

TEST(DecodeFromBytesAscii, EveryOctetIsItsOwnCodepointAndCostsOneByte)
{
  // UCS1/ASCII here is the byte as the codepoint, the whole 0x00..0xFF range
  constexpr auto WIRE{ Octets(0xE9u, 0xFFu, 0x41u) };
  auto source{ AsBytes(WIRE) };

  EXPECT_EQ(Pull(source, Encoding::ASCII).codepoint, std::optional{ 0x00E9u });
  EXPECT_EQ(source.size(), 2u);
  EXPECT_EQ(Pull(source, Encoding::UCS1 ).codepoint, std::optional{ 0x00FFu });
  EXPECT_EQ(source.size(), 1u);

  auto const last{ Pull(source, Encoding::ASCII) };
  EXPECT_EQ(last.codepoint, std::optional{ 0x0041u });
  EXPECT_EQ(last.consumed , 1u);
  EXPECT_TRUE(source.empty());
}

TEST(DecodeFromBytesAscii, AnEmptySourceAnswersNothingAndConsumesNothing)
{
  Bytes source{ };
  auto const pulled{ Pull(source, Encoding::ASCII) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
}

TEST(DecodeFromBytesUcs2, TheSameCharacterInBothByteOrders)
{
  constexpr auto LITTLE{ Octets(0xE9u, 0x00u) };
  constexpr auto BIG   { Octets(0x00u, 0xE9u) };

  auto little{ AsBytes(LITTLE) };
  auto const from_little{ Pull(little, Encoding::UCS2, std::endian::little) };
  EXPECT_EQ(from_little.codepoint, std::optional{ 0x00E9u });
  EXPECT_EQ(from_little.consumed , 2u);
  EXPECT_TRUE(little.empty());

  auto big{ AsBytes(BIG) };
  auto const from_big{ Pull(big, Encoding::UCS2, std::endian::big) };
  EXPECT_EQ(from_big.codepoint, std::optional{ 0x00E9u });
  EXPECT_EQ(from_big.consumed , 2u);
  EXPECT_TRUE(big.empty());
}

TEST(DecodeFromBytesUcs2, ASingleLeftoverByteAnswersNothingAndDoesNotAdvance)
{
  constexpr auto WIRE{ Octets(0xE9u) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UCS2, std::endian::little) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
  EXPECT_EQ(source.size()   , 1u);
}

TEST(DecodeFromBytesUcs2, ASurrogateValueDecodesRawBecauseUcs2HasNoPairs)
{
  // surrogates are a UTF-16 concept: the same bytes read as UTF16 are a
  // failure and read as UCS2 are a character
  constexpr auto WIRE{ Octets(0x00u, 0xD8u) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UCS2, std::endian::little) };
  EXPECT_EQ(pulled.codepoint, std::optional{ 0xD800u });
  EXPECT_EQ(pulled.consumed , 2u);
}

TEST(DecodeFromBytesUcs4, TheSameCharacterInBothByteOrdersUnderEitherName)
{
  constexpr auto LITTLE{ Octets(0x00u, 0xF6u, 0x01u, 0x00u) };
  constexpr auto BIG   { Octets(0x00u, 0x01u, 0xF6u, 0x00u) };

  auto little{ AsBytes(LITTLE) };
  auto const from_little{ Pull(little, Encoding::UCS4, std::endian::little) };
  EXPECT_EQ(from_little.codepoint, std::optional{ 0x01F600u });
  EXPECT_EQ(from_little.consumed , 4u);

  auto big{ AsBytes(BIG) };
  auto const from_big{ Pull(big, Encoding::UTF32, std::endian::big) };
  EXPECT_EQ(from_big.codepoint, std::optional{ 0x01F600u });
  EXPECT_EQ(from_big.consumed , 4u);
}

TEST(DecodeFromBytesUcs4, AShortBufferAnswersNothingAndDoesNotAdvance)
{
  constexpr auto WIRE{ Octets(0x00u, 0xF6u, 0x01u) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UCS4, std::endian::little) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
  EXPECT_EQ(source.size()   , 3u);
}

TEST(DecodeFromBytesUtf8, EachCodepointConsumesExactlyItsOwnOctets)
{
  constexpr auto WIRE{ Octets(0xC3u, 0xA9u,                    // U+00E9
                              0xF0u, 0x9Fu, 0x98u, 0x80u,      // U+1F600
                              0x41u) };                        // U+0041
  auto source{ AsBytes(WIRE) };

  auto const latin{ Pull(source, Encoding::UTF8) };
  EXPECT_EQ(latin.codepoint, std::optional{ 0x00E9u });
  EXPECT_EQ(latin.consumed , 2u);

  auto const astral{ Pull(source, Encoding::UTF8) };
  EXPECT_EQ(astral.codepoint, std::optional{ 0x01F600u });
  EXPECT_EQ(astral.consumed , 4u);

  auto const ascii{ Pull(source, Encoding::UTF8) };
  EXPECT_EQ(ascii.codepoint, std::optional{ 0x0041u });
  EXPECT_EQ(ascii.consumed , 1u);
  EXPECT_TRUE(source.empty());
}

TEST(DecodeFromBytesUtf8, TheDefaultArgumentsAreUtf8InNativeOrder)
{
  constexpr auto WIRE{ Octets(0xC3u, 0xA9u) };
  auto source{ AsBytes(WIRE) };

  auto const value{ DecodeFromBytes<char32_t>(source) };
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(std::uint32_t{ *value }, 0x00E9u);
  EXPECT_TRUE(source.empty());
}

TEST(DecodeFromBytesUtf8, AnIncompleteTrailingSequenceLeavesTheSourceUntouched)
{
  constexpr auto WIRE{ Octets(0xE2u, 0x82u) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UTF8) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
  EXPECT_EQ(source.size()   , 2u);

  constexpr auto LEAD{ Octets(0xF0u) };
  auto lead{ AsBytes(LEAD) };
  EXPECT_EQ(Pull(lead, Encoding::UTF8).codepoint, std::nullopt);
  EXPECT_EQ(lead.size(), 1u);
}

TEST(DecodeFromBytesUtf8, AnUndecodableOctetIsTerminalAndConsumesNothing)
{
  constexpr auto WIRE{ Octets(0xFFu, 0x41u) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UTF8) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
  EXPECT_EQ(source.size()   , 2u);
}

TEST(DecodeFromBytesUtf8, AReplayedByteIsNotCommittedEvenThoughItDecodedOnItsOwn)
{
  // the replay answers 'A' and flags the sequence it broke on the same feed,
  // so committing it would consume the C2 that never decoded
  constexpr auto WIRE{ Octets(0xC2u, 0x41u) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UTF8) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
  EXPECT_EQ(source.size()   , 2u);
}

TEST(DecodeFromBytesUtf16, ASurrogatePairCostsFourBytes)
{
  constexpr auto LITTLE{ Octets(0x3Du, 0xD8u, 0x00u, 0xDEu) };
  constexpr auto BIG   { Octets(0xD8u, 0x3Du, 0xDEu, 0x00u) };

  auto little{ AsBytes(LITTLE) };
  auto const from_little{ Pull(little, Encoding::UTF16, std::endian::little) };
  EXPECT_EQ(from_little.codepoint, std::optional{ 0x01F600u });
  EXPECT_EQ(from_little.consumed , 4u);
  EXPECT_TRUE(little.empty());

  auto big{ AsBytes(BIG) };
  auto const from_big{ Pull(big, Encoding::UTF16, std::endian::big) };
  EXPECT_EQ(from_big.codepoint, std::optional{ 0x01F600u });
  EXPECT_EQ(from_big.consumed , 4u);
  EXPECT_TRUE(big.empty());
}

TEST(DecodeFromBytesUtf16, TheByteOrderArgumentIsHonoured)
{
  constexpr auto WIRE{ Octets(0x00u, 0xE9u) };

  auto little{ AsBytes(WIRE) };
  EXPECT_EQ(Pull(little, Encoding::UTF16, std::endian::little).codepoint,
            std::optional{ 0xE900u });

  auto big{ AsBytes(WIRE) };
  EXPECT_EQ(Pull(big, Encoding::UTF16, std::endian::big).codepoint,
            std::optional{ 0x00E9u });
}

TEST(DecodeFromBytesUtf16, AnOddTrailingByteAnswersNothingAndDoesNotAdvance)
{
  constexpr auto WIRE{ Octets(0x41u) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UTF16, std::endian::native) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
  EXPECT_EQ(source.size()   , 1u);
}

TEST(DecodeFromBytesUtf16, ALoneLowSurrogateRunsTheLoopDryAndLeavesTheSourceAlone)
{
  // nullopt conflates "need more bytes" with "these bytes will never decode",
  // so a caller holding a complete buffer must read no progress as terminal
  constexpr auto WIRE{ Octets(0x00u, 0xDCu) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UTF16, std::endian::native) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
  EXPECT_EQ(source.size()   , 2u);
}

TEST(DecodeFromBytesUtf16, AHighSurrogateWithoutItsLowPartialsOutAtTheBufferEnd)
{
  constexpr auto WIRE{ Octets(0x3Du, 0xD8u) };
  auto source{ AsBytes(WIRE) };

  auto const pulled{ Pull(source, Encoding::UTF16, std::endian::native) };
  EXPECT_EQ(pulled.codepoint, std::nullopt);
  EXPECT_EQ(pulled.consumed , 0u);
  EXPECT_EQ(source.size()   , 2u);
}

TEST(DecodeFromBytesUtf16, BmpUnitsWalkTheBufferTwoBytesAtATime)
{
  constexpr auto WIRE{ Octets(0xE9u, 0x00u, 0xFFu, 0xFEu) };
  auto source{ AsBytes(WIRE) };

  EXPECT_EQ(Pull(source, Encoding::UTF16, std::endian::native).codepoint,
            std::optional{ 0x00E9u });
  EXPECT_EQ(source.size(), 2u);
  EXPECT_EQ(Pull(source, Encoding::UTF16, std::endian::native).codepoint,
            std::optional{ 0xFEFFu });
  EXPECT_TRUE(source.empty());
}

namespace
{
  struct Sunk
  {
    std::size_t                written;
    std::size_t                advanced;
    std::vector<std::uint32_t> octets;   // what actually landed in the room
  };

  template <std::size_t N>
  auto Sink(std::array<std::byte, N>& room, std::uint32_t codepoint,
            Encoding encoding, std::endian order) -> Sunk
  {
    auto       dest  { AsWritableBytes(room) };
    auto const before{ dest.size() };
    auto const written{ EncodeIntoBytes(static_cast<char32_t>(codepoint),
                                        dest, encoding, order) };
    auto octets{ std::vector<std::uint32_t>{ } };
    for (auto const octet : std::span{ room }.first(std::min<std::size_t>(written, N)))
      { octets.push_back(std::to_integer<std::uint32_t>(octet)); }
    return { static_cast<std::size_t>(written), before - dest.size(),
             std::move(octets) };
  }

  template <std::integral... _Octets>
  auto ExpectSunk(std::uint32_t codepoint, Encoding encoding, std::endian order,
                  _Octets... octets) -> void
  {
    std::array<std::byte, 8u> room{ };
    auto const sunk  { Sink(room, codepoint, encoding, order) };
    auto const wanted{ std::vector<std::uint32_t>{ static_cast<std::uint32_t>(octets)... } };
    auto const label { std::format("EncodeIntoBytes(U+{:04X})", codepoint) };
    EXPECT_EQ(sunk.octets  , wanted)        << label;
    EXPECT_EQ(sunk.written , wanted.size()) << label << ": byte count";
    EXPECT_EQ(sunk.advanced, wanted.size()) << label << ": span advance";
  }
}

TEST(EncodeIntoBytesUcs1, OneByteHoldsOneCodepointUnderEitherName)
{
  ExpectSunk(0x41u, Encoding::UCS1 , std::endian::native, 0x41u);
  ExpectSunk(0xE9u, Encoding::ASCII, std::endian::native, 0xE9u);
  ExpectSunk(0xFFu, Encoding::ASCII, std::endian::native, 0xFFu);
}

TEST(EncodeIntoBytesUcs1, RefusesAValueTooWideForItsUnit)
{
  // refusal answers 0; a negative answer is reserved for "would fit, no room"
  ExpectSunk(0x0001F600u, Encoding::UCS1, std::endian::native);
  ExpectSunk(0x00000141u, Encoding::UCS1, std::endian::native);
  ExpectSunk(0x0000D800u, Encoding::UCS1, std::endian::native);
}

TEST(EncodeIntoBytesUcs2, TheSameCharacterInBothByteOrders)
{
  ExpectSunk(0xE9u, Encoding::UCS2, std::endian::little, 0xE9u, 0x00u);
  ExpectSunk(0xE9u, Encoding::UCS2, std::endian::big   , 0x00u, 0xE9u);
}

TEST(EncodeIntoBytesUcs2, RefusesAnAstralValueAndStoresASurrogateValueWhole)
{
  // the check is width, not meaning: a surrogate value fits sixteen bits and
  // is stored as itself
  ExpectSunk(0x0001F600u, Encoding::UCS2, std::endian::little);
  ExpectSunk(0x0000D800u, Encoding::UCS2, std::endian::little, 0x00u, 0xD8u);
}

TEST(EncodeIntoBytesUcs4, TheSameCharacterInBothByteOrdersUnderEitherName)
{
  ExpectSunk(0x01F600u, Encoding::UCS4 , std::endian::little, 0x00u, 0xF6u, 0x01u, 0x00u);
  ExpectSunk(0x01F600u, Encoding::UTF32, std::endian::big   , 0x00u, 0x01u, 0xF6u, 0x00u);
}

TEST(EncodeIntoBytesUcs4, StoresTheValueWholeWhateverItMeans)
{
  ExpectSunk(0x00110000u, Encoding::UCS4, std::endian::little, 0x00u, 0x00u, 0x11u, 0x00u);
  ExpectSunk(0x0000D800u, Encoding::UCS4, std::endian::little, 0x00u, 0xD8u, 0x00u, 0x00u);
}

TEST(EncodeIntoBytesUtf8, EveryWidthLandsAsItsOwnOctets)
{
  constexpr auto ORDER{ std::endian::native };
  ExpectSunk(0x000041u, Encoding::UTF8, ORDER, 0x41u);
  ExpectSunk(0x0000E9u, Encoding::UTF8, ORDER, 0xC3u, 0xA9u);
  ExpectSunk(0x0020ACu, Encoding::UTF8, ORDER, 0xE2u, 0x82u, 0xACu);
  ExpectSunk(0x01F600u, Encoding::UTF8, ORDER, 0xF0u, 0x9Fu, 0x98u, 0x80u);
  ExpectSunk(0x10FFFFu, Encoding::UTF8, ORDER, 0xF4u, 0x8Fu, 0xBFu, 0xBFu);
}

TEST(EncodeIntoBytesUtf8, SurrogateAndOutOfRangeInputBecomeTheReplacement)
{
  constexpr auto ORDER{ std::endian::native };
  ExpectSunk(0x0000D800u, Encoding::UTF8, ORDER, 0xEFu, 0xBFu, 0xBDu);
  ExpectSunk(0x00110000u, Encoding::UTF8, ORDER, 0xEFu, 0xBFu, 0xBDu);
  ExpectSunk(0xFFFFFFFFu, Encoding::UTF8, ORDER, 0xEFu, 0xBFu, 0xBDu);
}

TEST(EncodeIntoBytesUtf8, TheDefaultArgumentsAreUtf8InNativeOrder)
{
  std::array<std::byte, 8u> room{ };
  auto dest{ AsWritableBytes(room) };

  EXPECT_EQ(EncodeIntoBytes(U'é', dest), 2u);
  EXPECT_EQ(room[0u], std::byte{ 0xC3u });
  EXPECT_EQ(room[1u], std::byte{ 0xA9u });
  EXPECT_EQ(dest.size(), 6u);
}

TEST(EncodeIntoBytesUtf16, BmpCodepointsAreOneUnitInTheOrderAsked)
{
  ExpectSunk(0xE9u, Encoding::UTF16, std::endian::little, 0xE9u, 0x00u);
  ExpectSunk(0xE9u, Encoding::UTF16, std::endian::big   , 0x00u, 0xE9u);
}

TEST(EncodeIntoBytesUtf16, AstralCodepointsAreASurrogatePairInTheOrderAsked)
{
  ExpectSunk(0x01F600u, Encoding::UTF16, std::endian::little,
             0x3Du, 0xD8u, 0x00u, 0xDEu);
  ExpectSunk(0x01F600u, Encoding::UTF16, std::endian::big,
             0xD8u, 0x3Du, 0xDEu, 0x00u);
}

TEST(EncodeIntoBytesUtf16, SurrogateAndOutOfRangeInputBecomeOneReplacementUnit)
{
  ExpectSunk(0x0000D800u, Encoding::UTF16, std::endian::little, 0xFDu, 0xFFu);
  ExpectSunk(0x00110000u, Encoding::UTF16, std::endian::little, 0xFDu, 0xFFu);
}

TEST(EncodeIntoBytesRoom, AFixedWidthUnitThatDoesNotFitReportsHowShortTheRoomFell)
{
  std::array<std::byte, 3u> room{ };

  auto ascii{ AsWritableBytes(std::span{ room }.first(0u)) };
  EXPECT_EQ(EncodeIntoBytes(U'A', ascii, Encoding::ASCII, std::endian::native), -1);
  EXPECT_TRUE(ascii.empty());

  auto ucs2{ AsWritableBytes(std::span{ room }.first(1u)) };
  EXPECT_EQ(EncodeIntoBytes(U'A', ucs2, Encoding::UCS2, std::endian::native), -1);
  EXPECT_EQ(ucs2.size(), 1u);

  auto ucs4{ AsWritableBytes(room) };
  EXPECT_EQ(EncodeIntoBytes(U'A', ucs4, Encoding::UCS4, std::endian::native), -1);
  EXPECT_EQ(ucs4.size(), 3u);
}

TEST(EncodeIntoBytesRoom, AVariableWidthCodepointTooBigForTheRoomIsRefusedWhole)
{
  // everything past the consumed length is indeterminate scratch, so neither
  // the caller nor this test may read it
  std::array<std::byte, 3u> room{ };
  auto dest{ AsWritableBytes(room) };

  EXPECT_EQ(EncodeIntoBytes(U'\U0001F600', dest, Encoding::UTF8,
                            std::endian::native), -1);
  EXPECT_EQ(dest.size(), 3u);                   // not advanced, as promised
}

TEST(EncodeIntoBytesRoom, AnExactFitIsTakenWhole)
{
  std::array<std::byte, 4u> room{ };
  auto dest{ AsWritableBytes(room) };

  EXPECT_EQ(EncodeIntoBytes(U'\U0001F600', dest, Encoding::UTF8,
                            std::endian::native), 4);
  EXPECT_TRUE(dest.empty());
  EXPECT_EQ(room[0u], std::byte{ 0xF0u });
  EXPECT_EQ(room[3u], std::byte{ 0x80u });
}

TEST(EncodeIntoBytesRoom, AUtf16PairTooBigForTheRoomIsRefusedWhole)
{
  // the shortfall counts the whole unit that could not be placed, hence -2
  std::array<std::byte, 2u> room{ };
  auto dest{ AsWritableBytes(room) };

  EXPECT_EQ(EncodeIntoBytes(U'\U0001F600', dest, Encoding::UTF16,
                            std::endian::little), -2);
  EXPECT_EQ(dest.size(), 2u);
}

TEST(EncodeIntoBytesRoundTrip, WhatWasWrittenReadsBackAsTheSameCodepoint)
{
  constexpr auto SAMPLES{ std::array<std::uint32_t, 5u>{
    0x000041u, 0x0000E9u, 0x0020ACu, 0x01F600u, 0x10FFFFu } };
  constexpr auto WIDE{ std::array<Encoding, 3u>{
    Encoding::UCS4, Encoding::UTF8, Encoding::UTF16 } };

  constexpr auto ORDERS{ std::array<std::endian, 2u>{
    std::endian::little, std::endian::big } };

  for (auto const order : ORDERS) {
  for (auto const encoding : WIDE) {
    for (auto const codepoint : SAMPLES) {
      std::array<std::byte, 8u> room{ };
      auto written{ AsWritableBytes(room) };
      auto const count{ EncodeIntoBytes(static_cast<char32_t>(codepoint), written,
                                        encoding, order) };
      auto read{ Bytes{ std::span{ room }.first(count) } };
      auto const back{ DecodeFromBytes<char32_t>(read, encoding, order) };
      auto const label{ std::format("U+{:04X} through encoding {} order {}",
                                    codepoint, static_cast<int>(encoding),
                                    order == std::endian::little ? "LE" : "BE") };
      ASSERT_TRUE(back.has_value()) << label;
      EXPECT_EQ(std::uint32_t{ *back }, codepoint) << label;
      EXPECT_TRUE(read.empty()) << label << ": reader did not consume it all"; } } }
}

TEST(EncodeIntoBytesRoundTrip, AForeignOrderUtf16BufferReadsBackAsWritten)
{
  std::array<std::byte, 4u> room{ };
  auto written{ AsWritableBytes(room) };
  auto const count{ EncodeIntoBytes(U'é', written, Encoding::UTF16,
                                    std::endian::big) };
  ASSERT_EQ(count, 2u);
  EXPECT_EQ(room[0u], std::byte{ 0x00u });
  EXPECT_EQ(room[1u], std::byte{ 0xE9u });

  auto read{ Bytes{ std::span{ room }.first(count) } };
  auto const back{ DecodeFromBytes<char32_t>(read, Encoding::UTF16,
                                             std::endian::big) };
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(std::uint32_t{ *back }, 0x00E9u);
  EXPECT_TRUE(read.empty());

  auto ucs2{ Bytes{ std::span{ room }.first(count) } };
  auto const honoured{ DecodeFromBytes<char32_t>(ucs2, Encoding::UCS2,
                                                 std::endian::big) };
  ASSERT_TRUE(honoured.has_value());
  EXPECT_EQ(std::uint32_t{ *honoured }, 0x00E9u);
}
