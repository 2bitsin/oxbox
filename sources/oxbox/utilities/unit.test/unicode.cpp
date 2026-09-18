#include "oxbox/utilities/unicode.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using namespace oxbox::utilities;

namespace
{
  struct Transcoded
  {
    std::size_t                count;
    std::vector<std::uint32_t> units;
  };

  template <std::integral _Unit, std::integral _From>
  auto Transcode(UtfDecodeState& state, _From input) -> Transcoded
  {
    auto const [count, encoded]{ UtfTranscode<_Unit>(state, input) };
    return { std::size_t{ count }, { encoded.begin(), encoded.begin() + count } };
  }
}

TEST(UtfTranscode, MidSequenceUnitsProduceNoUnitsAtAll)
{
  UtfDecodeState state{ };
  EXPECT_EQ(Transcode<char16_t>(state, char8_t{ 0xE2u }).count, 0u);
  EXPECT_EQ(Transcode<char16_t>(state, char8_t{ 0x82u }).count, 0u);

  auto const completed{ Transcode<char16_t>(state, char8_t{ 0xACu }) };
  EXPECT_EQ(completed.count, 1u);
  EXPECT_EQ(completed.units, (std::vector<std::uint32_t>{ 0x20ACu }));
}

TEST(UtfTranscode, AstralUtf8OctetsBecomeAUtf16SurrogatePairOnTheLastOctet)
{
  UtfDecodeState state{ };
  for (auto const octet : { 0xF0u, 0x9Fu, 0x98u }) {
    EXPECT_EQ(Transcode<char16_t>(state, static_cast<char8_t>(octet)).count, 0u)
      << std::format("octet {:#04x}", octet); }

  auto const pair{ Transcode<char16_t>(state, char8_t{ 0x80u }) };
  EXPECT_EQ(pair.count, 2u);
  EXPECT_EQ(pair.units, (std::vector<std::uint32_t>{ 0xD83Du, 0xDE00u }));
}

TEST(UtfTranscode, AUtf16SurrogatePairBecomesFourUtf8Octets)
{
  UtfDecodeState state{ };
  EXPECT_EQ(Transcode<char8_t>(state, char16_t{ 0xD83Du }).count, 0u);

  auto const octets{ Transcode<char8_t>(state, char16_t{ 0xDE00u }) };
  EXPECT_EQ(octets.count, 4u);
  EXPECT_EQ(octets.units, (std::vector<std::uint32_t>{ 0xF0u, 0x9Fu, 0x98u, 0x80u }));
}

TEST(UtfTranscode, Utf32ToUtf32IsIdentityOneUnitPerUnit)
{
  UtfDecodeState state{ };
  auto const ascii{ Transcode<char32_t>(state, char32_t{ 0x000041u }) };
  EXPECT_EQ(ascii.count, 1u);
  EXPECT_EQ(ascii.units, (std::vector<std::uint32_t>{ 0x41u }));

  auto const astral{ Transcode<char32_t>(state, char32_t{ 0x10FFFFu }) };
  EXPECT_EQ(astral.count, 1u);
  EXPECT_EQ(astral.units, (std::vector<std::uint32_t>{ 0x10FFFFu }));
}

TEST(UtfTranscode, AnInvalidUnitProducesTheReplacement)
{
  UtfDecodeState state{ };
  auto const stray{ Transcode<char16_t>(state, char8_t{ 0x80u }) };
  EXPECT_EQ(stray.count, 1u);
  EXPECT_EQ(stray.units, (std::vector<std::uint32_t>{ 0xFFFDu }));

  UtfDecodeState wide{ };
  auto const surrogate{ Transcode<char8_t>(wide, char32_t{ 0xD800u }) };
  EXPECT_EQ(surrogate.count, 3u);
  EXPECT_EQ(surrogate.units, (std::vector<std::uint32_t>{ 0xEFu, 0xBFu, 0xBDu }));
}

namespace
{
  // Independent literals catch a shared encoder/decoder mistake in any
  // of the nine crossings, including unchanged combining code points.
  template <typename _Dest, typename _From>
  auto ExpectCrossing(std::basic_string_view<_From> source,
                      std::basic_string_view<_Dest> expected) -> void
  {
    auto state{ UtfDecodeState{ } };
    auto actual{ std::basic_string<_Dest>{ } };
    for (auto const unit : source)
    {
      auto const [count, encoded]{ UtfTranscode<_Dest>(state, unit) };
      actual.append(encoded.begin(), encoded.begin() + count);
    }
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(static_cast<unsigned>(state.step), 0u);
  }

  auto ExpectCrossings(std::u8string_view utf8, std::u16string_view utf16,
                       std::u32string_view utf32) -> void
  {
    ExpectCrossing(utf8 , utf8 );
    ExpectCrossing(utf8 , utf16);
    ExpectCrossing(utf8 , utf32);
    ExpectCrossing(utf16, utf8 );
    ExpectCrossing(utf16, utf16);
    ExpectCrossing(utf16, utf32);
    ExpectCrossing(utf32, utf8 );
    ExpectCrossing(utf32, utf16);
    ExpectCrossing(utf32, utf32);
  }
}

TEST(UtfTranscode, AsciiTextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"Hello, world! 1234 ~!@#$%^&*()_+-=[]{};':,./<>?",
                   u"Hello, world! 1234 ~!@#$%^&*()_+-=[]{};':,./<>?",
                   U"Hello, world! 1234 ~!@#$%^&*()_+-=[]{};':,./<>?");
}

TEST(UtfTranscode, Latin1TextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"© ® ± µ ÷ ¢ £ ¥",
                   u"© ® ± µ ÷ ¢ £ ¥",
                   U"© ® ± µ ÷ ¢ £ ¥");
}

TEST(UtfTranscode, GreekTextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"Ω Σ π αβγδεζηθ",
                   u"Ω Σ π αβγδεζηθ",
                   U"Ω Σ π αβγδεζηθ");
}

TEST(UtfTranscode, CyrillicTextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"Привет мир",
                   u"Привет мир",
                   U"Привет мир");
}

TEST(UtfTranscode, HiraganaTextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"こんにちは世界",
                   u"こんにちは世界",
                   U"こんにちは世界");
}

TEST(UtfTranscode, CjkTextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"漢字テスト",
                   u"漢字テスト",
                   U"漢字テスト");
}

TEST(UtfTranscode, ArabicTextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"مرحبا بالعالم",
                   u"مرحبا بالعالم",
                   U"مرحبا بالعالم");
}

TEST(UtfTranscode, DevanagariTextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"नमस्ते दुनिया",
                   u"नमस्ते दुनिया",
                   U"नमस्ते दुनिया");
}

TEST(UtfTranscode, EmojiTextCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"😀😃😄😁😆😅😂🤣😊🙂",
                   u"😀😃😄😁😆😅😂🤣😊🙂",
                   U"😀😃😄😁😆😅😂🤣😊🙂");
}

TEST(UtfTranscode, MixedWidthsCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"A©Ω「界」😀Z",
                   u"A©Ω「界」😀Z",
                   U"A©Ω「界」😀Z");
}

TEST(UtfTranscode, WidthBoundariesCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"\u007F\u0080\u07FF\u0800\uFFFF",
                   u"\u007F\u0080\u07FF\u0800\uFFFF",
                   U"\u007F\u0080\u07FF\u0800\uFFFF");
}

TEST(UtfTranscode, AstralEndpointsCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"\U00010000\U0001F600\U0010FFFF",
                   u"\U00010000\U0001F600\U0010FFFF",
                   U"\U00010000\U0001F600\U0010FFFF");
}

TEST(UtfTranscode, CombiningMarksCrossEveryUtfWidth)
{
  // Every source width must produce the independently spelled destination.
  ExpectCrossings(u8"é ä ô",
                   u"é ä ô",
                   U"é ä ô");
}

TEST(UtfTranscode, InvalidWideValuesBecomeReplacementInEveryUtfWidth)
{
  // UTF code units validate scalars even when both widths are 32 bits.
  // The raw UCS4 byte interface deliberately has a different contract.
  for (auto const value : { char32_t{ 0x110000u }, char32_t{ 0xD800u },
                           char32_t{ 0xDFFFu } })
  {
    auto const source{ std::u32string_view{ &value, 1u } };
    ExpectCrossing(source, std::u8string_view{ u8"\uFFFD" });
    ExpectCrossing(source, std::u16string_view{ u"\uFFFD" });
    ExpectCrossing(source, std::u32string_view{ U"\uFFFD" });
  }
}
