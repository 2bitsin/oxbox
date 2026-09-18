#include "oxbox/utilities/utf-encode.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <tuple>
#include <vector>

using namespace oxbox::utilities;

namespace
{
  template <std::integral _Unit, std::integral... _Units>
  auto ExpectEncoded(std::uint32_t codepoint, _Units... units) -> void
  {
    auto const [count, encoded]{ UtfEncode<_Unit>(codepoint) };
    auto const label{ std::format("UtfEncode<{}-bit>(U+{:04X})",
                                  8u * sizeof(_Unit), codepoint) };
    ASSERT_LE(std::size_t{ count }, encoded.size()) << label << ": count overruns the array";
    auto const wanted{ std::vector<std::uint32_t>{ static_cast<std::uint32_t>(units)... } };
    auto const got   { std::vector<std::uint32_t>{ encoded.begin(),
                                                   encoded.begin() + count } };
    EXPECT_EQ(got, wanted) << label;
  }
}

TEST(UtfEncodeToUtf8, AsciiIsASingleOctet)
{
  ExpectEncoded<char8_t>(0x41u, 0x41u);
  ExpectEncoded<char8_t>(0x7Fu, 0x7Fu);
}

TEST(UtfEncodeToUtf8, TwoOctetRangeEndpoints)
{
  ExpectEncoded<char8_t>(0x080u, 0xC2u, 0x80u);
  ExpectEncoded<char8_t>(0x7FFu, 0xDFu, 0xBFu);
}

TEST(UtfEncodeToUtf8, ThreeOctetRangeEndpoints)
{
  ExpectEncoded<char8_t>(0x0800u, 0xE0u, 0xA0u, 0x80u);
  ExpectEncoded<char8_t>(0xFFFFu, 0xEFu, 0xBFu, 0xBFu);
}

TEST(UtfEncodeToUtf8, FourOctetRangeEndpoints)
{
  ExpectEncoded<char8_t>(0x010000u, 0xF0u, 0x90u, 0x80u, 0x80u);
  ExpectEncoded<char8_t>(0x01F600u, 0xF0u, 0x9Fu, 0x98u, 0x80u);
  ExpectEncoded<char8_t>(0x10FFFFu, 0xF4u, 0x8Fu, 0xBFu, 0xBFu);
}

TEST(UtfEncodeToUtf8, SurrogateCodepointsBecomeTheReplacement)
{
  // a surrogate is not a codepoint anything may encode; CESU-8 is not on offer
  ExpectEncoded<char8_t>(0xD800u, 0xEFu, 0xBFu, 0xBDu);
  ExpectEncoded<char8_t>(0xDC00u, 0xEFu, 0xBFu, 0xBDu);
  ExpectEncoded<char8_t>(0xDFFFu, 0xEFu, 0xBFu, 0xBDu);
}

TEST(UtfEncodeToUtf8, OutOfRangeCodepointsBecomeTheReplacement)
{
  ExpectEncoded<char8_t>(0x00110000u, 0xEFu, 0xBFu, 0xBDu);
  ExpectEncoded<char8_t>(0xFFFFFFFFu, 0xEFu, 0xBFu, 0xBDu);
}

TEST(UtfEncodeToUtf16, TheWholeBmpIsOneUnitUnchanged)
{
  ExpectEncoded<char16_t>(0x0041u, 0x0041u);
  ExpectEncoded<char16_t>(0x07FFu, 0x07FFu);
  ExpectEncoded<char16_t>(0x0800u, 0x0800u);
  ExpectEncoded<char16_t>(0xFFFFu, 0xFFFFu);
}

TEST(UtfEncodeToUtf16, AstralCodepointsBecomeASurrogatePair)
{
  ExpectEncoded<char16_t>(0x010000u, 0xD800u, 0xDC00u);
  ExpectEncoded<char16_t>(0x01F600u, 0xD83Du, 0xDE00u);
  ExpectEncoded<char16_t>(0x10FFFFu, 0xDBFFu, 0xDFFFu);
}

TEST(UtfEncodeToUtf16, SurrogateOrOutOfRangeInputBecomesOneReplacementUnit)
{
  ExpectEncoded<char16_t>(0xD800u    , 0xFFFDu);
  ExpectEncoded<char16_t>(0xDFFFu    , 0xFFFDu);
  ExpectEncoded<char16_t>(0x00110000u, 0xFFFDu);
  ExpectEncoded<char16_t>(0xFFFFFFFFu, 0xFFFDu);
}

TEST(UtfEncodeToUtf32, ValidCodepointsPassThroughAsOneUnit)
{
  ExpectEncoded<char32_t>(0x000041u, 0x000041u);
  ExpectEncoded<char32_t>(0x00FFFFu, 0x00FFFFu);
  ExpectEncoded<char32_t>(0x010000u, 0x010000u);
  ExpectEncoded<char32_t>(0x10FFFFu, 0x10FFFFu);
}

TEST(UtfEncodeToUtf32, SurrogateOrOutOfRangeInputBecomesTheReplacement)
{
  ExpectEncoded<char32_t>(0xD800u    , 0xFFFDu);
  ExpectEncoded<char32_t>(0xDC00u    , 0xFFFDu);
  ExpectEncoded<char32_t>(0x00110000u, 0xFFFDu);
  ExpectEncoded<char32_t>(0xFFFFFFFFu, 0xFFFDu);
}

TEST(UtfEncodeIsConstexpr, CountsAndUnitsAreAvailableAtCompileTime)
{
  static_assert(std::get<0u>(UtfEncode<char8_t >(0x01F600u)) == 4u);
  static_assert(std::get<1u>(UtfEncode<char8_t >(0x01F600u))[0u] == char8_t{ 0xF0u });
  static_assert(std::get<0u>(UtfEncode<char16_t>(0x10FFFFu)) == 2u);
  static_assert(std::get<1u>(UtfEncode<char16_t>(0x10FFFFu))[1u] == char16_t{ 0xDFFFu });
  static_assert(std::get<0u>(UtfEncode<char32_t>(0xD800u   )) == 1u);

  ExpectEncoded<char8_t>(0x01F600u, 0xF0u, 0x9Fu, 0x98u, 0x80u);
}
