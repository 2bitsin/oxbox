#include "oxbox/utilities/transcode.hpp"

#include "oxbox/utilities/unit.test/octets.hpp"
#include "oxbox/utilities/unit.test/resilient-walk.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <ranges>
#include <vector>

using namespace oxbox::utilities;
using oxbox::utilities::test::AsValues;
using oxbox::utilities::test::Octets;
using oxbox::utilities::test::UTF16_BE;
using oxbox::utilities::test::UTF16_LE;
using oxbox::utilities::test::Walk;

namespace
{
  // The bytes an append built, in a shape gtest can print.
  auto Appended(std::vector<char32_t> const& codepoints, TextFormat format = { })
    -> std::vector<std::uint32_t>
  {
    auto out{ std::vector<std::byte>{ } };
    for (auto const codepoint : codepoints)
      { EncodeAppend(codepoint, std::back_inserter(out), format); }
    return out | std::views::transform([](std::byte octet)
                   { return std::to_integer<std::uint32_t>(octet); })
               | std::ranges::to<std::vector>();
  }
}

// ====================== SniffByteOrderMark =========================

TEST(SniffByteOrderMark, EachEncodingsOwnMarkIsConsumed)
{
  constexpr auto UTF8_WIRE{ Octets(0xEFu, 0xBBu, 0xBFu, 0x41u) };
  auto utf8{ Bytes{ UTF8_WIRE } };
  EXPECT_EQ(SniffByteOrderMark(utf8,
    { Encoding::UTF8, ByteOrder::READ_MARK }).encoding, Encoding::UTF8);
  EXPECT_EQ(utf8.size(), 1u);

  constexpr auto UTF16_WIRE{ Octets(0xFEu, 0xFFu, 0x00u, 0x41u) };
  auto utf16{ Bytes{ UTF16_WIRE } };
  SniffByteOrderMark(utf16,
    { Encoding::UTF16, ByteOrder::READ_MARK });
  EXPECT_EQ(utf16.size(), 2u);

  constexpr auto UCS4_WIRE{ Octets(0x00u, 0x00u, 0xFEu, 0xFFu,
                                   0x00u, 0x00u, 0x00u, 0x41u) };
  auto ucs4{ Bytes{ UCS4_WIRE } };
  SniffByteOrderMark(ucs4, { Encoding::UCS4, ByteOrder::READ_MARK });
  EXPECT_EQ(ucs4.size(), 4u);
}

TEST(SniffByteOrderMark, AStatedOrderOutranksTheMark)
{
  constexpr auto WIRE{ Octets(0xFFu, 0xFEu, 0xE9u, 0x00u) };
  for (auto const declared : { UTF16_BE, UTF16_LE })
  {
    auto source{ Bytes{ WIRE } };
    auto const format{ SniffByteOrderMark(source, declared) };
    EXPECT_EQ(format.order, declared.order);
    EXPECT_EQ(source.size(), WIRE.size());
    auto const expected{ declared.order == std::endian::little
      ? std::vector<std::uint32_t>{ 0xFEFFu, 0xE9u }
      : std::vector<std::uint32_t>{ 0xFFFEu, 0xE900u } };
    EXPECT_EQ(Walk(source, format).spelled, expected);
  }
}

TEST(SniffByteOrderMark, TheFourByteMarksAreToldApartFromTheTwoByteOnes)
{
  // FF FE opens both a little-endian UTF-16 mark and a little-endian
  // UCS4 one; only the declared encoding says which is being read.
  constexpr auto WIRE{ Octets(0xFFu, 0xFEu, 0x00u, 0x00u) };

  auto ucs4{ Bytes{ WIRE } };
  EXPECT_EQ(SniffByteOrderMark(ucs4, { Encoding::UCS4, ByteOrder::READ_MARK }).order,
            std::endian::little);
  EXPECT_TRUE(ucs4.empty());

  auto utf16{ Bytes{ WIRE } };
  EXPECT_EQ(SniffByteOrderMark(utf16,
    { Encoding::UTF16, ByteOrder::READ_MARK }).order, std::endian::little);
  EXPECT_EQ(utf16.size(), 2u);
}

TEST(SniffByteOrderMark, AnotherEncodingsMarkIsJustText)
{
  constexpr auto UTF8_MARK{ Octets(0xEFu, 0xBBu, 0xBFu) };
  auto utf16{ Bytes{ UTF8_MARK } };
  EXPECT_EQ(SniffByteOrderMark(utf16,
    { Encoding::UTF16, ByteOrder::READ_MARK }).order, std::endian::native);
  EXPECT_EQ(utf16.size(), 3u);                  // untouched

  constexpr auto UTF16_MARK{ Octets(0xFFu, 0xFEu) };
  auto utf8{ Bytes{ UTF16_MARK } };
  SniffByteOrderMark(utf8, { Encoding::UTF8, ByteOrder::READ_MARK });
  EXPECT_EQ(utf8.size(), 2u);                   // untouched
}

TEST(SniffByteOrderMark, TheFixedWidthTwoByteEncodingReadsTheSameMark)
{
  // UCS2 is not UTF-16, but it is two bytes wide and a mark means the
  // same thing to it: which end of the unit came first.
  constexpr auto WIRE{ Octets(0xFFu, 0xFEu, 0xE9u, 0x00u) };
  auto source{ Bytes{ WIRE } };

  auto const format{ SniffByteOrderMark(
    source, { Encoding::UCS2, ByteOrder::READ_MARK }) };
  EXPECT_EQ(format.order   , std::endian::little);
  EXPECT_EQ(format.encoding, Encoding::UCS2);
  EXPECT_EQ(source.size(), 2u);
  EXPECT_EQ(Walk(source, format).spelled, (std::vector<std::uint32_t>{ 0xE9u }));
}

TEST(SniffByteOrderMark, RawOctetsHaveNoMarkToSpell)
{
  constexpr auto WIRE{ Octets(0xEFu, 0xBBu, 0xBFu) };
  auto source{ Bytes{ WIRE } };
  SniffByteOrderMark(source, { Encoding::ASCII, ByteOrder::READ_MARK });
  EXPECT_EQ(source.size(), 3u);
}

TEST(SniffByteOrderMark, NoMarkLeavesBothTheSourceAndTheFormatAlone)
{
  constexpr auto WIRE{ Octets(0x41u, 0x42u, 0x43u) };
  auto source{ Bytes{ WIRE } };

  auto const format{ SniffByteOrderMark(source, UTF16_BE) };
  EXPECT_EQ(format.order   , std::endian::big);
  EXPECT_EQ(format.encoding, Encoding::UTF16);
  EXPECT_EQ(source.size(), 3u);
}

TEST(SniffByteOrderMark, ASourceShorterThanTheMarkIsNotConsumed)
{
  // A mark cut by the end of the buffer is a refill's problem. Eating
  // the fragment would destroy the bytes that complete it.
  constexpr auto CUT_UTF8{ Octets(0xEFu, 0xBBu) };
  auto utf8{ Bytes{ CUT_UTF8 } };
  SniffByteOrderMark(utf8, { Encoding::UTF8, ByteOrder::READ_MARK });
  EXPECT_EQ(utf8.size(), 2u);

  constexpr auto CUT_UTF16{ Octets(0xFFu) };
  auto utf16{ Bytes{ CUT_UTF16 } };
  EXPECT_EQ(SniffByteOrderMark(utf16,
    { Encoding::UTF16, ByteOrder::READ_MARK }).order, std::endian::native);
  EXPECT_EQ(utf16.size(), 1u);

  auto nothing{ Bytes{ } };
  SniffByteOrderMark(nothing,
    { Encoding::UTF8, ByteOrder::READ_MARK });
  EXPECT_TRUE(nothing.empty());
}

// ====================== DecodeResilient ============================

TEST(DecodeResilient, WalksACleanBufferWithNoRepairsAndNothingLeftOver)
{
  constexpr auto WIRE{ Octets(0x41u,                          // U+0041
                              0xC3u, 0xA9u,                   // U+00E9
                              0xE2u, 0x82u, 0xACu,            // U+20AC
                              0xF0u, 0x9Fu, 0x98u, 0x80u) };  // U+1F600
  auto const walked{ Walk(Bytes{ WIRE }) };

  EXPECT_EQ(walked.spelled,
            (std::vector<std::uint32_t>{ 0x41u, 0xE9u, 0x20ACu, 0x01F600u }));
  EXPECT_EQ(walked.replacements, 0u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(DecodeResilient, DamageBecomesAReplacementAndTheStreamResynchronises)
{
  // The case the whole policy exists for. E2 82 opens a three-octet
  // sequence that 41 does not finish -- but 41 is a perfectly good 'A'
  // and MUST survive. The walk slides one unit at a time, so it spells
  // a replacement for the ruined lead, another for the orphaned
  // continuation, and then reads the 'A' that was there all along.
  constexpr auto WIRE{ Octets(0xE2u, 0x82u, 0x41u) };
  auto const walked{ Walk(Bytes{ WIRE }) };

  EXPECT_EQ(walked.spelled,
            (std::vector<std::uint32_t>{ 0xFFFDu, 0xFFFDu, 0x41u }));
  EXPECT_EQ(walked.replacements, 2u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(DecodeResilient, ARunOfGarbageIsRepairedAndTheTextAfterItSurvives)
{
  constexpr auto WIRE{ Octets(0x41u, 0xFFu, 0xFEu, 0xFDu, 0xFCu, 0x42u) };
  auto const walked{ Walk(Bytes{ WIRE }) };

  EXPECT_EQ(walked.spelled, (std::vector<std::uint32_t>{
    0x41u, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0x42u }));
  EXPECT_EQ(walked.replacements, 4u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(DecodeResilient, AnOrphanedContinuationOctetCostsExactlyOneReplacement)
{
  constexpr auto WIRE{ Octets(0x41u, 0x80u, 0x42u) };
  auto const walked{ Walk(Bytes{ WIRE }) };

  EXPECT_EQ(walked.spelled,
            (std::vector<std::uint32_t>{ 0x41u, 0xFFFDu, 0x42u }));
  EXPECT_EQ(walked.replacements, 1u);
}

TEST(DecodeResilient, ATruncatedTailIsLeftForARefillRatherThanReplaced)
{
  // The line between damage and impatience. These two octets are the
  // start of a well-formed sequence; replacing them would corrupt text
  // that is merely incomplete, so they stay in the caller's span.
  constexpr auto WIRE{ Octets(0x41u, 0xE2u, 0x82u) };
  auto const walked{ Walk(Bytes{ WIRE }) };

  EXPECT_EQ(walked.spelled     , (std::vector<std::uint32_t>{ 0x41u }));
  EXPECT_EQ(walked.replacements, 0u);
  EXPECT_EQ(walked.left        , 2u);
}

TEST(DecodeResilient, AnEmptySourceIsACleanEmptyWalk)
{
  auto const walked{ Walk(Bytes{ }) };
  EXPECT_TRUE(walked.spelled.empty());
  EXPECT_EQ(walked.replacements, 0u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(DecodeResilient, TheSlideIsOneCodeUnitSoUtf16StaysAligned)
{
  // A high surrogate followed by a plain unit is damage, and the repair
  // must slide a whole UNIT past it. A byte-wide slide would land
  // half-way into the next unit and read a character that was never on
  // the wire, so the surviving 'A' is what proves the alignment.
  constexpr auto WIRE{ Octets(0x3Du, 0xD8u, 0x41u, 0x00u) };
  auto const walked{ Walk(Bytes{ WIRE }, UTF16_LE) };

  EXPECT_EQ(walked.spelled     , (std::vector<std::uint32_t>{ 0xFFFDu, 0x41u }));
  EXPECT_EQ(walked.replacements, 1u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(DecodeResilient, AUtf16PairCutByTheBufferEndIsCarriedNotReplaced)
{
  constexpr auto WIRE{ Octets(0xE9u, 0x00u, 0x3Du, 0xD8u) };
  auto const walked{ Walk(Bytes{ WIRE }, UTF16_LE) };

  EXPECT_EQ(walked.spelled     , (std::vector<std::uint32_t>{ 0xE9u }));
  EXPECT_EQ(walked.replacements, 0u);
  EXPECT_EQ(walked.left        , 2u);
}

TEST(DecodeResilient, AnOddTrailingByteIsHalfAUnitAndIsCarried)
{
  constexpr auto WIRE{ Octets(0xE9u, 0x00u, 0x41u) };
  auto const walked{ Walk(Bytes{ WIRE }, UTF16_LE) };

  EXPECT_EQ(walked.spelled     , (std::vector<std::uint32_t>{ 0xE9u }));
  EXPECT_EQ(walked.replacements, 0u);
  EXPECT_EQ(walked.left        , 1u);
}

TEST(DecodeResilient, FinishTurnsADanglingPartialIntoOneReplacement)
{
  // However many bytes of it arrived, an unfinished sequence was one
  // codepoint's worth of intent, so it costs exactly one replacement.
  constexpr auto WIRE{ Octets(0xF0u, 0x9Fu, 0x98u) };
  auto source{ Bytes{ WIRE } };

  auto spelled{ std::vector<char32_t>{ } };
  auto const walked{ DecodeResilient(source, std::back_inserter(spelled)) };
  ASSERT_EQ(walked.codepoints, 0u);
  ASSERT_EQ(source.size()    , 3u);

  auto const finished{ FinishDecode(source, std::back_inserter(spelled)) };
  EXPECT_EQ(finished.codepoints  , 1u);
  EXPECT_EQ(finished.replacements, 1u);
  EXPECT_EQ(AsValues(spelled), (std::vector<std::uint32_t>{ 0xFFFDu }));
  EXPECT_TRUE(source.empty());
}

TEST(DecodeResilient, FinishOnACleanlyDrainedSourceAddsNothing)
{
  auto source{ Bytes{ } };
  auto spelled{ std::vector<char32_t>{ } };

  auto const finished{ FinishDecode(source, std::back_inserter(spelled)) };
  EXPECT_EQ(finished.codepoints  , 0u);
  EXPECT_EQ(finished.replacements, 0u);
  EXPECT_TRUE(spelled.empty());
}

TEST(DecodeResilient, TheFixedWidthEncodingsWalkWithoutEverNeedingARepair)
{
  constexpr auto ASCII{ Octets(0x41u, 0xE9u, 0xFFu) };
  auto const ascii{ Walk(Bytes{ ASCII }, { Encoding::ASCII, std::endian::native }) };
  EXPECT_EQ(ascii.spelled, (std::vector<std::uint32_t>{ 0x41u, 0xE9u, 0xFFu }));
  EXPECT_EQ(ascii.replacements, 0u);

  constexpr auto UCS4{ Octets(0x00u, 0xF6u, 0x01u, 0x00u,
                              0x41u, 0x00u, 0x00u, 0x00u) };
  auto const ucs4{ Walk(Bytes{ UCS4 }, { Encoding::UCS4, std::endian::little }) };
  EXPECT_EQ(ucs4.spelled, (std::vector<std::uint32_t>{ 0x01F600u, 0x41u }));
  EXPECT_EQ(ucs4.replacements, 0u);
}

TEST(DecodeResilient, WritesThroughAnEncodeIteratorSoARepairedStreamReEncodes)
{
  // The sink is anything a codepoint can be written to and stepped
  // past, which is what lets a damaged stream be repaired straight into
  // an output buffer with no vector in between.
  constexpr auto WIRE{ Octets(0x41u, 0xE2u, 0x82u, 0x41u) };
  auto source{ Bytes{ WIRE } };

  std::array<std::byte, 16u> room{ };
  auto target{ AsWritableBytes(room) };
  BufferEncodeIterator sink{ target, Encoding::UTF8, std::endian::native };

  auto const report{ DecodeResilient(source, sink) };
  EXPECT_EQ(report.codepoints  , 4u);   // 'A', two repairs, 'A'
  EXPECT_EQ(report.replacements, 2u);

  auto written{ Bytes{ room }.first(room.size() - target.size()) };
  EXPECT_EQ(Walk(written).spelled, (std::vector<std::uint32_t>{
    0x41u, 0xFFFDu, 0xFFFDu, 0x41u }));
}

// ====================== EncodeAppend ===============================

TEST(EncodeAppend, GrowsTheOutputByEachCodepointsOwnOctets)
{
  EXPECT_EQ(Appended({ U'A', U'é', U'€', U'\U0001F600' }),
            (std::vector<std::uint32_t>{ 0x41u,
                                         0xC3u, 0xA9u,
                                         0xE2u, 0x82u, 0xACu,
                                         0xF0u, 0x9Fu, 0x98u, 0x80u }));
}

TEST(EncodeAppend, HonoursTheEncodingAndByteOrderItWasAsked)
{
  EXPECT_EQ(Appended({ U'é', U'\U0001F600' }, UTF16_BE),
            (std::vector<std::uint32_t>{ 0x00u, 0xE9u,
                                         0xD8u, 0x3Du, 0xDEu, 0x00u }));
  EXPECT_EQ(Appended({ U'é', U'\U0001F600' }, UTF16_LE),
            (std::vector<std::uint32_t>{ 0xE9u, 0x00u,
                                         0x3Du, 0xD8u, 0x00u, 0xDEu }));
}

TEST(EncodeAppend, ACodepointTheEncodingCannotSpellAppendsNothing)
{
  // The primitive refuses a value too wide for its unit, and appending
  // inherits that: the output grows by the codepoints that fit and by
  // nothing else, rather than by a silently truncated byte.
  constexpr auto ASCII{ TextFormat{ Encoding::ASCII, std::endian::native } };
  EXPECT_EQ(Appended({ U'A', U'\U0001F600', U'B' }, ASCII),
            (std::vector<std::uint32_t>{ 0x41u, 0x42u }));
}

TEST(EncodeAppend, RoundTripsThroughTheResilientWalk)
{
  auto const SAMPLES{ std::vector<char32_t>{
    U'A', U'é', U'€', U'\U0001F600', U'\U0010FFFF' } };
  constexpr auto FORMATS{ std::array<TextFormat, 4u>{
    TextFormat{ Encoding::UTF8 , std::endian::native },
    TextFormat{ Encoding::UTF16, std::endian::little },
    TextFormat{ Encoding::UTF16, std::endian::big    },
    TextFormat{ Encoding::UCS4 , std::endian::big    } } };

  for (auto const format : FORMATS)
  {
    auto wire{ std::vector<std::byte>{ } };
    for (auto const codepoint : SAMPLES)
      { EncodeAppend(codepoint, std::back_inserter(wire), format); }

    auto const label { std::format("encoding {} order {}",
                                   static_cast<int>(format.encoding),
                                   format.order == std::endian::little ? "LE" : "BE") };
    auto const walked{ Walk(Bytes{ wire }, format) };
    EXPECT_EQ(walked.spelled, AsValues(SAMPLES)) << label;
    EXPECT_EQ(walked.replacements, 0u) << label << ": a clean buffer needed a repair";
    EXPECT_EQ(walked.left        , 0u) << label << ": something was left behind";
  }
}

TEST(DecodeResilient, EveryLeadAboveFourBytesIsReplacedWithoutEatingText)
{
  // Both a lone bad lead and one between letters must reach U+FFFD;
  // checking the whole ceiling catches F8 as well as the all-ones FF.
  for (auto octet{ 0xF8u }; octet <= 0xFFu; ++octet)
  {
    auto const lone{ Octets(octet) };
    auto const surrounded{ Octets(0x61u, octet, 0x62u) };
    auto const alone{ Walk(Bytes{ lone }) };
    auto const between{ Walk(Bytes{ surrounded }) };
    EXPECT_EQ(alone.spelled, (std::vector<std::uint32_t>{ 0xFFFDu }));
    EXPECT_EQ(between.spelled,
              (std::vector<std::uint32_t>{ 0x61u, 0xFFFDu, 0x62u }));
    EXPECT_EQ(alone.replacements, 1u);
    EXPECT_EQ(between.replacements, 1u);
    EXPECT_EQ(alone.left, 0u);
    EXPECT_EQ(between.left, 0u);
  }
}

TEST(DecodeResilient, BrokenUtf16SurrogatesAreReplacedThroughEndOfStream)
{
  // A lone low is damage now; the second of two highs needs Finish.
  for (auto const units : { std::vector<char16_t>{ 0xDC00u },
                           std::vector<char16_t>{ 0xD800u, 0xD800u } })
  {
    auto source{ AsBytes(units) };
    auto spelled{ std::vector<char32_t>{ } };
    auto const report{ DecodeResilient(source, std::back_inserter(spelled),
                         { Encoding::UTF16, std::endian::native }) };
    auto const finish{ FinishDecode(source, std::back_inserter(spelled)) };
    EXPECT_EQ(spelled, std::vector<char32_t>(units.size(), U'\uFFFD'));
    EXPECT_EQ(report.replacements + finish.replacements, units.size());
    EXPECT_TRUE(source.empty());
  }
}

TEST(DecodeResilient, EveryRawOctetWidensAndReencodesWithoutLoss)
{
  // ASCII here means all 256 byte values, including NUL and the top half.
  auto wire{ std::vector<std::byte>{ } };
  auto expected{ std::vector<char32_t>{ } };
  for (auto value{ 0u }; value < 256u; ++value)
  {
    wire.push_back(static_cast<std::byte>(value));
    expected.push_back(static_cast<char32_t>(value));
  }
  auto source{ Bytes{ wire } };
  auto points{ std::vector<char32_t>{ } };
  constexpr auto RAW{ TextFormat{ Encoding::ASCII, std::endian::native } };
  auto const report{ DecodeResilient(source, std::back_inserter(points),
                                     RAW) };
  EXPECT_EQ(points, expected);
  EXPECT_EQ(report.codepoints, 256u);
  EXPECT_EQ(report.replacements, 0u);
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(Appended(points, RAW), AsValues(expected));

  // The upper octets need two UTF-8 bytes, with independently computed
  // wire values so a symmetric round trip cannot hide a wrong spelling.
  auto utf8{ std::vector<std::uint32_t>{ } };
  for (auto value{ 0u }; value < 256u; ++value)
  {
    if (value < 128u) { utf8.push_back(value); }
    else
    {
      utf8.push_back(0xC0u | (value >> 6u));
      utf8.push_back(0x80u | (value & 0x3Fu));
    }
  }
  EXPECT_EQ(Appended(points), utf8);
}

TEST(DecodeResilient, CopiesUtf8IntoABigEndianUtf16EncodeIterator)
{
  // The crossing must change encoding and order, not merely reproduce
  // UTF-8 through a UTF-8 sink. Check the destination's actual octets.
  constexpr auto WIRE{ Octets(0x63u, 0x61u, 0x66u, 0xC3u, 0xA9u) };
  constexpr auto EXPECTED{ Octets(0x00u, 0x63u, 0x00u, 0x61u,
                                  0x00u, 0x66u, 0x00u, 0xE9u) };
  auto source{ Bytes{ WIRE } };
  auto room{ std::array<std::byte, 8u>{ } };
  auto target{ AsWritableBytes(room) };
  auto sink{ BufferEncodeIterator{ target, Encoding::UTF16,
                                   std::endian::big } };
  auto const report{ DecodeResilient(source, sink) };
  EXPECT_EQ(room, EXPECTED);
  EXPECT_TRUE(target.empty());
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(report.codepoints, 4u);
  EXPECT_EQ(report.replacements, 0u);
}

TEST(ChunkDecoder, RawOctetsStayWholeAcrossFeeds)
{
  // No byte value can start a carry in the byte-per-character format.
  constexpr auto WIRE{ Octets(0x61u, 0xE9u, 0xFFu) };
  auto decoder{ ChunkDecoder{
    { Encoding::ASCII, std::endian::native } } };
  auto points{ std::vector<char32_t>{ } };
  for (auto const& octet : WIRE)
  {
    auto const report{ decoder.Consume(Bytes{ &octet, 1u },
                                        std::back_inserter(points)) };
    EXPECT_EQ(report.codepoints, 1u);
    EXPECT_EQ(report.replacements, 0u);
    EXPECT_EQ(decoder.Pending(), 0u);
  }
  EXPECT_EQ(AsValues(points),
            (std::vector<std::uint32_t>{ 0x61u, 0xE9u, 0xFFu }));
  EXPECT_EQ(decoder.Finish(std::back_inserter(points)).codepoints, 0u);
}

TEST(ChunkDecoder, AStatedOrderReencodesAnOddFeedIntoUtf8)
{
  // Constructor order must survive an odd feed and reach a differently
  // encoded destination, in both orders rather than only native order.
  constexpr auto BIG{ Octets(0x00u, 0xE9u, 0x00u, 0x21u) };
  constexpr auto LITTLE{ Octets(0xE9u, 0x00u, 0x21u, 0x00u) };
  for (auto const format : { UTF16_BE, UTF16_LE })
  {
    auto const wire{ format.order == std::endian::big
                      ? Bytes{ BIG } : Bytes{ LITTLE } };
    auto decoder{ ChunkDecoder{ format } };
    auto room{ std::array<std::byte, 3u>{ } };
    auto target{ AsWritableBytes(room) };
    auto sink{ BufferEncodeIterator{ target } };
    EXPECT_EQ(decoder.Consume(wire.first(3u), sink).codepoints, 1u);
    EXPECT_EQ(decoder.Pending(), 1u);
    EXPECT_EQ(decoder.Consume(wire.subspan(3u), sink).codepoints, 1u);
    EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
    EXPECT_EQ(decoder.Format().order, format.order);
    EXPECT_EQ(room, Octets(0xC3u, 0xA9u, 0x21u));
    EXPECT_TRUE(target.empty());
  }
}

TEST(ChunkDecoder, FinishingHalfAUtf16UnitReplacesItOnlyOnce)
{
  // End of stream turns half a unit into one replacement and clears
  // the carry; finishing a second time must not repeat that character.
  constexpr auto HALF{ Octets(0xE9u) };
  auto decoder{ ChunkDecoder{ UTF16_LE } };
  auto points{ std::vector<char32_t>{ } };
  auto const consumed{ decoder.Consume(Bytes{ HALF },
                                        std::back_inserter(points)) };
  EXPECT_EQ(consumed.codepoints, 0u);
  EXPECT_EQ(consumed.replacements, 0u);
  EXPECT_TRUE(points.empty());
  EXPECT_EQ(decoder.Pending(), 1u);
  auto const first{ decoder.Finish(std::back_inserter(points)) };
  auto const second{ decoder.Finish(std::back_inserter(points)) };
  EXPECT_EQ(first.codepoints, 1u);
  EXPECT_EQ(first.replacements, 1u);
  EXPECT_EQ(second.codepoints, 0u);
  EXPECT_EQ(second.replacements, 0u);
  EXPECT_EQ(decoder.Pending(), 0u);
  EXPECT_EQ(AsValues(points), (std::vector<std::uint32_t>{ 0xFFFDu }));
}
