#include "oxbox/utilities/transcode.hpp"

#include "oxbox/utilities/unit.test/octets.hpp"
#include "oxbox/utilities/unit.test/resilient-walk.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

using namespace oxbox::utilities;
using oxbox::utilities::test::AsValues;
using oxbox::utilities::test::Octets;
using oxbox::utilities::test::UTF16_LE;
using oxbox::utilities::test::Walk;
using oxbox::utilities::test::Walked;

namespace
{
  // Every chunk in turn through one decoder, then end of stream. `left`
  // is what was still carried when the last chunk had been read --
  // asked BEFORE the flush, which zeroes the carry unconditionally and
  // would make the answer a foregone 0.
  auto WalkChunks(std::vector<Bytes> const& chunks, TextFormat format = { }) -> Walked
  {
    auto spelled{ std::vector<char32_t>{ } };
    auto decoder{ ChunkDecoder{ format } };
    auto report { DecodeReport{ } };
    for (auto const chunk : chunks)
    { auto const consumed{ decoder.Consume(chunk, std::back_inserter(spelled)) };
      report.codepoints   += consumed.codepoints;
      report.replacements += consumed.replacements; }

    auto const carried { decoder.Pending() };
    auto const finished{ decoder.Finish(std::back_inserter(spelled)) };
    report.codepoints   += finished.codepoints;
    report.replacements += finished.replacements;

    EXPECT_EQ(report.codepoints, spelled.size()) << "the report miscounted what it wrote";
    EXPECT_EQ(decoder.Pending(), 0u) << "the flush left a carry behind";
    return { AsValues(spelled), report.replacements, carried };
  }

  // One byte per chunk -- the worst boundary placement a stream can hand
  // a decoder, and the one that finds carry bugs.
  auto WalkOneByteAtATime(Bytes whole, TextFormat format = { }) -> Walked
  {
    auto chunks{ std::vector<Bytes>{ } };
    for (auto index{ std::size_t{ 0u } }; index < whole.size(); ++index)
      { chunks.push_back(whole.subspan(index, 1u)); }
    return WalkChunks(chunks, format);
  }
}

// ====================== ChunkDecoder ===============================

TEST(ChunkDecoder, ASequenceSplitBetweenChunksSurvivesConcatenation)
{
  constexpr auto FIRST { Octets(0x41u, 0xF0u, 0x9Fu) };
  constexpr auto SECOND{ Octets(0x98u, 0x80u, 0x42u) };
  auto const walked{ WalkChunks({ Bytes{ FIRST }, Bytes{ SECOND } }) };

  EXPECT_EQ(walked.spelled,
            (std::vector<std::uint32_t>{ 0x41u, 0x01F600u, 0x42u }));
  EXPECT_EQ(walked.replacements, 0u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(ChunkDecoder, AUnitSplitBetweenChunksSurvivesConcatenation)
{
  // The boundary falls INSIDE a UTF-16 code unit -- half a unit is not
  // even a decodable thing, so the carry is what makes it readable.
  constexpr auto FIRST { Octets(0xE9u) };
  constexpr auto SECOND{ Octets(0x00u, 0x41u, 0x00u) };
  auto const walked{ WalkChunks({ Bytes{ FIRST }, Bytes{ SECOND } }, UTF16_LE) };

  EXPECT_EQ(walked.spelled     , (std::vector<std::uint32_t>{ 0xE9u, 0x41u }));
  EXPECT_EQ(walked.replacements, 0u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(ChunkDecoder, ASurrogatePairSplitBetweenChunksSurvivesConcatenation)
{
  constexpr auto FIRST { Octets(0x3Du, 0xD8u) };
  constexpr auto SECOND{ Octets(0x00u, 0xDEu) };
  auto const walked{ WalkChunks({ Bytes{ FIRST }, Bytes{ SECOND } }, UTF16_LE) };

  EXPECT_EQ(walked.spelled     , (std::vector<std::uint32_t>{ 0x01F600u }));
  EXPECT_EQ(walked.replacements, 0u);
}

TEST(ChunkDecoder, DamageAcrossAChunkBoundaryIsRepairedTheSameAsDamageWithin)
{
  // The stitched boundary is walked by the same resync as the body, so
  // splitting the E2 82 41 case in two must not change its answer.
  constexpr auto WHOLE { Octets(0xE2u, 0x82u, 0x41u) };
  constexpr auto FIRST { Octets(0xE2u) };
  constexpr auto SECOND{ Octets(0x82u, 0x41u) };

  auto const together{ WalkChunks({ Bytes{ WHOLE } }) };
  auto const split   { WalkChunks({ Bytes{ FIRST }, Bytes{ SECOND } }) };

  EXPECT_EQ(split.spelled     , together.spelled);
  EXPECT_EQ(split.replacements, together.replacements);
  EXPECT_EQ(split.spelled,
            (std::vector<std::uint32_t>{ 0xFFFDu, 0xFFFDu, 0x41u }));
}

TEST(ChunkDecoder, ADanglingPartialBecomesOneReplacementOnFinish)
{
  constexpr auto WIRE{ Octets(0x41u, 0xE2u, 0x82u) };
  auto spelled{ std::vector<char32_t>{ } };
  auto decoder{ ChunkDecoder{ } };

  auto const consumed{ decoder.Consume(Bytes{ WIRE },
                                       std::back_inserter(spelled)) };
  EXPECT_EQ(consumed.codepoints  , 1u);
  EXPECT_EQ(consumed.replacements, 0u);
  EXPECT_EQ(decoder.Pending()    , 2u);   // carried, not replaced

  auto const finished{ decoder.Finish(std::back_inserter(spelled)) };
  EXPECT_EQ(finished.replacements, 1u);
  EXPECT_EQ(decoder.Pending()    , 0u);
  EXPECT_EQ(AsValues(spelled), (std::vector<std::uint32_t>{ 0x41u, 0xFFFDu }));
}

TEST(ChunkDecoder, AChunkThatEndsOnASequenceBoundaryCarriesNothing)
{
  constexpr auto WIRE{ Octets(0x41u, 0xC3u, 0xA9u) };
  auto spelled{ std::vector<char32_t>{ } };
  auto decoder{ ChunkDecoder{ } };

  decoder.Consume(Bytes{ WIRE }, std::back_inserter(spelled));
  EXPECT_EQ(decoder.Pending(), 0u);
  EXPECT_EQ(AsValues(spelled), (std::vector<std::uint32_t>{ 0x41u, 0xE9u }));
}

TEST(ChunkDecoder, AnEmptyChunkChangesNothing)
{
  constexpr auto WIRE{ Octets(0xF0u, 0x9Fu) };
  auto spelled{ std::vector<char32_t>{ } };
  auto decoder{ ChunkDecoder{ } };

  decoder.Consume(Bytes{ WIRE }, std::back_inserter(spelled));
  ASSERT_EQ(decoder.Pending(), 2u);

  decoder.Consume(Bytes{ }, std::back_inserter(spelled));
  EXPECT_EQ(decoder.Pending(), 2u);        // still waiting for the rest
  EXPECT_TRUE(spelled.empty());
}

TEST(ChunkDecoder, OneByteAtATimeReadsTheSameTextAsOneWholeBuffer)
{
  // The worst boundary placement there is: every sequence, every unit
  // and every surrogate pair is cut. The answer must not change.
  constexpr auto WIRE{ Octets(0x41u,                          // U+0041
                              0xC3u, 0xA9u,                   // U+00E9
                              0xE2u, 0x82u, 0xACu,            // U+20AC
                              0xF0u, 0x9Fu, 0x98u, 0x80u,     // U+1F600
                              0x42u) };
  auto const whole { Walk(Bytes{ WIRE }) };
  auto const pieces{ WalkOneByteAtATime(Bytes{ WIRE }) };

  EXPECT_EQ(pieces.spelled     , whole.spelled);
  EXPECT_EQ(pieces.replacements, 0u);
  EXPECT_EQ(pieces.left        , 0u);
}

TEST(ChunkDecoder, OneByteAtATimeRepairsDamageExactlyOnce)
{
  constexpr auto WIRE{ Octets(0x41u, 0xFFu, 0xFEu, 0x42u) };
  auto const whole { Walk(Bytes{ WIRE }) };
  auto const pieces{ WalkOneByteAtATime(Bytes{ WIRE }) };

  EXPECT_EQ(pieces.spelled     , whole.spelled);
  EXPECT_EQ(pieces.replacements, whole.replacements);
  EXPECT_EQ(pieces.spelled, (std::vector<std::uint32_t>{
    0x41u, 0xFFFDu, 0xFFFDu, 0x42u }));
}

TEST(ChunkDecoder, OneByteAtATimeReadsUtf16WhileTheCarryHoldsHalfAUnit)
{
  // The carry has to hold one and a half units here: a whole high
  // surrogate plus the first byte of its low half, three bytes of state
  // that only exist because the chunks landed where they did.
  constexpr auto WIRE{ Octets(0xE9u, 0x00u,                    // U+00E9
                              0x3Du, 0xD8u, 0x00u, 0xDEu,      // U+1F600
                              0x41u, 0x00u) };
  auto const whole { Walk(Bytes{ WIRE }, UTF16_LE) };
  auto const pieces{ WalkOneByteAtATime(Bytes{ WIRE }, UTF16_LE) };

  EXPECT_EQ(pieces.spelled, whole.spelled);
  EXPECT_EQ(pieces.spelled, (std::vector<std::uint32_t>{ 0xE9u, 0x01F600u, 0x41u }));
  EXPECT_EQ(pieces.replacements, 0u);
  EXPECT_EQ(pieces.left        , 0u);
}

TEST(ChunkDecoder, ACarriedHighSurrogateMeetingAPlainUnitIsRepairedAtTheSeam)
{
  // Damage that only becomes damage once the next chunk arrives: the
  // high surrogate was a perfectly good pending prefix until the head
  // of the following chunk turned out not to be its low half.
  constexpr auto FIRST { Octets(0x3Du, 0xD8u) };
  constexpr auto SECOND{ Octets(0x41u, 0x00u) };
  auto const walked{ WalkChunks({ Bytes{ FIRST }, Bytes{ SECOND } }, UTF16_LE) };

  EXPECT_EQ(walked.spelled     , (std::vector<std::uint32_t>{ 0xFFFDu, 0x41u }));
  EXPECT_EQ(walked.replacements, 1u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(ChunkDecoder, AFourByteUnitSplitDownTheMiddleSurvivesConcatenation)
{
  // UCS4 has no sequences to resynchronise, only units -- and half a
  // unit is still the carry's problem.
  constexpr auto UCS4_LE{ TextFormat{ Encoding::UCS4, std::endian::little } };
  constexpr auto FIRST  { Octets(0x00u, 0xF6u) };
  constexpr auto SECOND { Octets(0x01u, 0x00u) };
  auto const walked{ WalkChunks({ Bytes{ FIRST }, Bytes{ SECOND } }, UCS4_LE) };

  EXPECT_EQ(walked.spelled     , (std::vector<std::uint32_t>{ 0x01F600u }));
  EXPECT_EQ(walked.replacements, 0u);
  EXPECT_EQ(walked.left        , 0u);
}

TEST(ChunkDecoder, TwoByteChunksReadUtf16TheSameAsOneWholeBuffer)
{
  constexpr auto WIRE{ Octets(0xE9u, 0x00u,                    // U+00E9
                              0x3Du, 0xD8u, 0x00u, 0xDEu,      // U+1F600
                              0x41u, 0x00u) };
  auto const whole{ Walk(Bytes{ WIRE }, UTF16_LE) };

  auto chunks{ std::vector<Bytes>{ } };
  for (auto index{ std::size_t{ 0u } }; index < WIRE.size(); index += 2u)
    { chunks.push_back(Bytes{ WIRE }.subspan(index, 2u)); }
  auto const pieces{ WalkChunks(chunks, UTF16_LE) };

  EXPECT_EQ(pieces.spelled, whole.spelled);
  EXPECT_EQ(pieces.spelled, (std::vector<std::uint32_t>{ 0xE9u, 0x01F600u, 0x41u }));
  EXPECT_EQ(pieces.replacements, 0u);
}
