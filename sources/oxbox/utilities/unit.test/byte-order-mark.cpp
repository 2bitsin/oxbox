#include "oxbox/utilities/transcode.hpp"

#include "oxbox/utilities/unit.test/octets.hpp"
#include "oxbox/utilities/unit.test/resilient-walk.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <vector>

using namespace oxbox::utilities;
using oxbox::utilities::test::Octets;
using oxbox::utilities::test::Walk;

namespace
{
  struct MarkedText
  {
    Encoding encoding;
    std::endian order;
    std::vector<std::byte> wire;
    std::size_t mark_size;
  };

  auto MarkedTexts() -> std::vector<MarkedText>
  {
    return {
      { Encoding::UTF8, std::endian::native,
        { std::byte{ 0xEF }, std::byte{ 0xBB }, std::byte{ 0xBF },
          std::byte{ 0x41 } }, 3u },
      { Encoding::UTF16, std::endian::big,
        { std::byte{ 0xFE }, std::byte{ 0xFF }, std::byte{ 0x00 },
          std::byte{ 0x41 } }, 2u },
      { Encoding::UTF16, std::endian::little,
        { std::byte{ 0xFF }, std::byte{ 0xFE }, std::byte{ 0x41 },
          std::byte{ 0x00 } }, 2u },
      { Encoding::UCS2, std::endian::big,
        { std::byte{ 0xFE }, std::byte{ 0xFF }, std::byte{ 0x00 },
          std::byte{ 0x41 } }, 2u },
      { Encoding::UCS2, std::endian::little,
        { std::byte{ 0xFF }, std::byte{ 0xFE }, std::byte{ 0x41 },
          std::byte{ 0x00 } }, 2u },
      { Encoding::UCS4, std::endian::big,
        { std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0xFE },
          std::byte{ 0xFF }, std::byte{ 0x00 }, std::byte{ 0x00 },
          std::byte{ 0x00 }, std::byte{ 0x41 } }, 4u },
      { Encoding::UCS4, std::endian::little,
        { std::byte{ 0xFF }, std::byte{ 0xFE }, std::byte{ 0x00 },
          std::byte{ 0x00 }, std::byte{ 0x41 }, std::byte{ 0x00 },
          std::byte{ 0x00 }, std::byte{ 0x00 } }, 4u }
    };
  }
}

TEST(SniffByteOrderMark, ReadMarkSettlesEveryEncodingAndBothOrders)
{
  for (auto const& text : MarkedTexts())
  {
    auto source{ Bytes{ text.wire } };
    auto const format{ SniffByteOrderMark(source,
      { text.encoding, ByteOrder::READ_MARK }) };
    EXPECT_EQ(format.order, text.order);
    EXPECT_EQ(source.size(), text.wire.size() - text.mark_size);
    EXPECT_EQ(Walk(source, format).spelled,
              (std::vector<std::uint32_t>{ 0x41u }));
  }
}

TEST(SniffByteOrderMark, AStatedOrderDeliversLeadingFeffInEveryEncoding)
{
  for (auto const& text : MarkedTexts())
  {
    auto source{ Bytes{ text.wire } };
    auto const format{ SniffByteOrderMark(source,
      { text.encoding, text.order }) };
    EXPECT_EQ(format.order, text.order);
    EXPECT_EQ(source.size(), text.wire.size());
    EXPECT_EQ(Walk(source, format).spelled,
              (std::vector<std::uint32_t>{ 0xFEFFu, 0x41u }));
  }
}

TEST(SniffByteOrderMark, NoMarkResolvesReadMarkToNative)
{
  constexpr auto WIRE{ Octets(0x41u, 0x00u, 0x00u, 0x00u) };
  for (auto const encoding : { Encoding::ASCII, Encoding::UTF8,
         Encoding::UCS2, Encoding::UTF16, Encoding::UCS4 })
  {
    auto source{ Bytes{ WIRE } };
    auto const format{ SniffByteOrderMark(source,
      { encoding, ByteOrder::READ_MARK }) };
    EXPECT_EQ(format.order, std::endian::native);
    EXPECT_FALSE(format.order.ReadsMark());
    EXPECT_EQ(source.size(), WIRE.size());
  }
}

TEST(ChunkDecoder, EachEncodingsSplitMarkSettlesWithoutBeingDelivered)
{
  for (auto const& text : MarkedTexts())
  {
    for (auto split{ 1u }; split < text.mark_size; ++split)
    {
      auto decoder{ ChunkDecoder{
        { text.encoding, ByteOrder::READ_MARK } } };
      auto points{ std::vector<char32_t>{ } };
      auto sink{ std::back_inserter(points) };
      auto const wire{ Bytes{ text.wire } };
      EXPECT_EQ(decoder.Consume(wire.first(split), sink).codepoints, 0u);
      EXPECT_EQ(decoder.Pending(), split);
      EXPECT_TRUE(decoder.Format().order.ReadsMark());
      EXPECT_EQ(decoder.Consume({ }, sink).codepoints, 0u);
      auto const mark{ decoder.Consume(
        wire.subspan(split, text.mark_size - split), sink) };
      EXPECT_EQ(mark.codepoints, 0u);
      EXPECT_EQ(mark.replacements, 0u);
      EXPECT_EQ(decoder.Pending(), 0u);
      EXPECT_EQ(decoder.Format().order, text.order);
      auto const body{ decoder.Consume(wire.subspan(text.mark_size),
                                        sink) };
      EXPECT_EQ(body.codepoints, 1u);
      EXPECT_EQ(body.replacements, 0u);
      EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
      EXPECT_EQ(points, (std::vector<char32_t>{ U'A' }));
    }
  }
}

TEST(ChunkDecoder, AStatedOrderDeliversLeadingFeffAcrossEverySplit)
{
  for (auto const& text : MarkedTexts())
  {
    for (auto split{ 0u }; split <= text.wire.size(); ++split)
    {
      auto decoder{ ChunkDecoder{ { text.encoding, text.order } } };
      auto points{ std::vector<char32_t>{ } };
      auto sink{ std::back_inserter(points) };
      auto const wire{ Bytes{ text.wire } };
      auto const first{ decoder.Consume(wire.first(split), sink) };
      auto const last{ decoder.Consume(wire.subspan(split), sink) };
      EXPECT_EQ(first.codepoints + last.codepoints, 2u);
      EXPECT_EQ(first.replacements + last.replacements, 0u);
      EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
      EXPECT_EQ(points, (std::vector<char32_t>{ U'\uFEFF', U'A' }));
      EXPECT_EQ(decoder.Format().order, text.order);
    }
  }
}

TEST(ChunkDecoder, UnmarkedInputDecodesNativeAfterFourOctets)
{
  constexpr auto NARROW{ std::array<char, 4u>{ 'A', 'B', 'C', 'D' } };
  constexpr auto TWO{ std::array<char16_t, 2u>{ u'A', u'B' } };
  constexpr auto FOUR{ std::array<char32_t, 1u>{ U'A' } };
  for (auto const encoding : { Encoding::ASCII, Encoding::UTF8,
         Encoding::UCS2, Encoding::UTF16, Encoding::UCS4 })
  {
    auto const wire{ encoding == Encoding::UCS4 ? AsBytes(FOUR)
      : encoding == Encoding::UCS2 || encoding == Encoding::UTF16
        ? AsBytes(TWO) : AsBytes(NARROW) };
    auto decoder{ ChunkDecoder{ { encoding, ByteOrder::READ_MARK } } };
    auto points{ std::vector<char32_t>{ } };
    auto sink{ std::back_inserter(points) };
    for (auto index{ 0u }; index < 3u; ++index)
    {
      EXPECT_EQ(decoder.Consume(wire.subspan(index, 1u), sink).codepoints,
                0u);
      EXPECT_EQ(decoder.Pending(), index + 1u);
    }
    auto const report{ decoder.Consume(wire.last(1u), sink) };
    auto const expected{ encoding == Encoding::UCS4
      ? std::vector<char32_t>{ U'A' }
      : encoding == Encoding::UCS2 || encoding == Encoding::UTF16
        ? std::vector<char32_t>{ U'A', U'B' }
        : std::vector<char32_t>{ U'A', U'B', U'C', U'D' } };
    EXPECT_EQ(points, expected);
    EXPECT_EQ(report.codepoints, expected.size());
    EXPECT_EQ(report.replacements, 0u);
    EXPECT_EQ(decoder.Format().order, std::endian::native);
    EXPECT_EQ(decoder.Pending(), 0u);
    EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
  }
}

TEST(ChunkDecoder, ShortUnmarkedInputIsDecodedAtFinish)
{
  constexpr auto WIRE{ Octets(0x41u, 0x42u, 0x43u) };
  for (auto length{ 0u }; length <= WIRE.size(); ++length)
  {
    auto decoder{ ChunkDecoder{
      { Encoding::UTF8, ByteOrder::READ_MARK } } };
    auto points{ std::vector<char32_t>{ } };
    auto sink{ std::back_inserter(points) };
    EXPECT_EQ(decoder.Consume(Bytes{ WIRE }.first(length), sink).codepoints,
              0u);
    auto const report{ decoder.Finish(sink) };
    EXPECT_EQ(report.codepoints, length);
    EXPECT_EQ(report.replacements, 0u);
    EXPECT_EQ(points, (std::vector<char32_t>{ U'A', U'B', U'C' }
                       | std::views::take(length)
                       | std::ranges::to<std::vector>()));
    EXPECT_EQ(decoder.Pending(), 0u);
    EXPECT_EQ(decoder.Format().order, std::endian::native);
    EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
  }
}

TEST(ChunkDecoder, TwoAndFourByteMarksAreToldApartAcrossFeeds)
{
  constexpr auto WIRE{ Octets(0xFFu, 0xFEu, 0x00u, 0x00u) };
  for (auto const encoding : { Encoding::UTF16, Encoding::UCS4 })
  {
    auto decoder{ ChunkDecoder{ { encoding, ByteOrder::READ_MARK } } };
    auto points{ std::vector<char32_t>{ } };
    auto sink{ std::back_inserter(points) };
    EXPECT_EQ(decoder.Consume(Bytes{ WIRE }.first(2u), sink).codepoints,
              0u);
    EXPECT_EQ(decoder.Format().order.ReadsMark(),
              encoding == Encoding::UCS4);
    auto const report{ decoder.Consume(Bytes{ WIRE }.last(2u), sink) };
    EXPECT_EQ(report.codepoints, encoding == Encoding::UTF16 ? 1u : 0u);
    EXPECT_EQ(points, encoding == Encoding::UTF16
      ? std::vector<char32_t>{ U'\0' } : std::vector<char32_t>{ });
    EXPECT_EQ(decoder.Format().order, std::endian::little);
    EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
  }
}

TEST(ChunkDecoder, IncompleteMarkAtFinishIsDecodedAndRepairedOnce)
{
  constexpr auto WIRE{ Octets(0xEFu, 0xBBu) };
  auto decoder{ ChunkDecoder{ { Encoding::UTF8, ByteOrder::READ_MARK } } };
  auto points{ std::vector<char32_t>{ } };
  auto sink{ std::back_inserter(points) };
  EXPECT_EQ(decoder.Consume(Bytes{ WIRE }, sink).codepoints, 0u);
  auto const report{ decoder.Finish(sink) };
  EXPECT_EQ(report.codepoints, 1u);
  EXPECT_EQ(report.replacements, 1u);
  EXPECT_EQ(points, (std::vector<char32_t>{ U'\uFFFD' }));
  EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
}

TEST(ChunkDecoder, MarkAndTextShareAFeedAndLaterFeffRemainsText)
{
  for (auto const& text : MarkedTexts())
  {
    for (auto split{ 0u }; split < text.wire.size(); ++split)
    {
      auto decoder{ ChunkDecoder{
        { text.encoding, ByteOrder::READ_MARK } } };
      auto points{ std::vector<char32_t>{ } };
      auto sink{ std::back_inserter(points) };
      auto const wire{ Bytes{ text.wire } };
      auto const first{ decoder.Consume(wire.first(split), sink) };
      auto const last{ decoder.Consume(wire.subspan(split), sink) };
      EXPECT_EQ(first.codepoints + last.codepoints, 1u);
      EXPECT_EQ(first.replacements + last.replacements, 0u);
      EXPECT_EQ(decoder.Consume(wire, sink).codepoints, 2u);
      EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
      EXPECT_EQ(points,
                (std::vector<char32_t>{ U'A', U'\uFEFF', U'A' }));
    }
  }
}

TEST(ChunkDecoder, ShortNativeUtf16DecodesBeforeRepairingItsTail)
{
  constexpr auto UNITS{ std::array<char16_t, 2u>{ u'A', u'B' } };
  for (auto length{ 1u }; length <= 3u; ++length)
  {
    auto decoder{ ChunkDecoder{
      { Encoding::UTF16, ByteOrder::READ_MARK } } };
    auto points{ std::vector<char32_t>{ } };
    auto sink{ std::back_inserter(points) };
    EXPECT_EQ(decoder.Consume(AsBytes(UNITS).first(length), sink).codepoints,
              0u);
    auto const report{ decoder.Finish(sink) };
    auto expected{ std::vector<char32_t>{ } };
    if (length >= 2u) { expected.push_back(U'A'); }
    if (length % 2u != 0u) { expected.push_back(U'\uFFFD'); }
    EXPECT_EQ(points, expected);
    EXPECT_EQ(report.codepoints, expected.size());
    EXPECT_EQ(report.replacements, length % 2u);
    EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
  }
}

TEST(ChunkDecoder, UnmarkedFourOctetBufferKeepsAPartialSequence)
{
  constexpr auto WIRE{ Octets(0x41u, 0x42u, 0xE2u, 0x82u, 0xACu) };
  auto decoder{ ChunkDecoder{ { Encoding::UTF8, ByteOrder::READ_MARK } } };
  auto points{ std::vector<char32_t>{ } };
  auto sink{ std::back_inserter(points) };
  auto const first{ decoder.Consume(Bytes{ WIRE }.first(4u), sink) };
  EXPECT_EQ(first.codepoints, 2u);
  EXPECT_EQ(first.replacements, 0u);
  EXPECT_EQ(decoder.Pending(), 2u);
  auto const last{ decoder.Consume(Bytes{ WIRE }.last(1u), sink) };
  EXPECT_EQ(last.codepoints, 1u);
  EXPECT_EQ(last.replacements, 0u);
  EXPECT_EQ(decoder.Finish(sink).codepoints, 0u);
  EXPECT_EQ(points, (std::vector<char32_t>{ U'A', U'B', U'\u20AC' }));
}
