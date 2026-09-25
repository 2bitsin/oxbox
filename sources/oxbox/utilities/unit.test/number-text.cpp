// The radix-marker grammar, xoctet's Go-to box rule, and the two directions
// agreeing.
#include "oxbox/utilities/number-text.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace
{
  using namespace oxbox::utilities;
  using namespace std::string_view_literals;
}

// ---- the marker, on its own -----------------------------------------

static_assert(RadixMarker("0x1f", Radix::DECIMAL)->radix == Radix::HEX);
static_assert(RadixMarker("0x1f", Radix::DECIMAL)->digits == "1f");
static_assert(RadixMarker("0b10", Radix::DECIMAL)->radix == Radix::BINARY);
static_assert(RadixMarker("0o17", Radix::DECIMAL)->radix == Radix::OCTAL);
static_assert(RadixMarker("b800h", Radix::DECIMAL)->radix == Radix::HEX);
static_assert(RadixMarker("b800h", Radix::DECIMAL)->digits == "b800");
static_assert(RadixMarker("1234", Radix::DECIMAL) == std::nullopt);
// under a hex reading `0b` is two digits and not a marker
static_assert(RadixMarker("0b1010", Radix::HEX) == std::nullopt);

// ---- parsing ---------------------------------------------------------

TEST(WholeNumber, AcceptsTheWholeStringAndNothingLess)
{
  EXPECT_EQ(detail::number_text::WholeNumber<int>("200"), 200);
  EXPECT_EQ(detail::number_text::WholeNumber<std::size_t>("1a", 16), 26u);
  EXPECT_EQ(detail::number_text::WholeNumber<int>("0"), 0);
}

TEST(WholeNumber, TrailingJunkIsRejectedNotTruncated)
{
  // the whole point: a prefix match here is a value silently misread
  EXPECT_FALSE(detail::number_text::WholeNumber<int>("12abc").has_value());
  EXPECT_FALSE(detail::number_text::WholeNumber<int>("12 ").has_value());
  EXPECT_FALSE(detail::number_text::WholeNumber<int>("1a").has_value());  // decimal
}

TEST(WholeNumber, EmptyIsAbsentAndNotZero)
{
  EXPECT_FALSE(detail::number_text::WholeNumber<int>("").has_value());
}

TEST(ParseNumber, ReadsEitherCase)
{
  EXPECT_EQ(ParseNumber<U32>("b800", Radix::HEX), 0xB800u);
  EXPECT_EQ(ParseNumber<U32>("B800", Radix::HEX), 0xB800u);
  EXPECT_EQ(ParseNumber<U32>("dEaD", Radix::HEX), 0xDEADu);
}

TEST(ParseNumber, ThePrefixIsOptionalAndMeansNothingElse)
{
  EXPECT_EQ(ParseNumber<U32>("0x1f"), 0x1Fu);
  EXPECT_EQ(ParseNumber<U32>("0X1F"), 0x1Fu);
  EXPECT_EQ(ParseNumber<U32>("1f", Radix::HEX), 0x1Fu);
}

TEST(ParseNumber, LeadingZerosAreJustDigits)
{
  EXPECT_EQ(ParseNumber<U32>("0000b8", Radix::HEX), 0xB8u);
  EXPECT_EQ(ParseNumber<U32>("0", Radix::HEX), 0u);
  // a leading zero is never octal
  EXPECT_EQ(ParseNumber<U32>("0755"), 755u);
  EXPECT_EQ(ParseNumber<U32>("0o755"), 493u);
}

TEST(ParseNumber, TrailingJunkIsRejectedNotTruncated)
{
  // a listing is full of tokens that start like an address
  EXPECT_EQ(ParseNumber<U32>("12ax", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("1f:", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("12abc"), std::nullopt);
}

TEST(ParseNumber, TheAssemblerSuffixIsAMarkerAndNotJunk)
{
  EXPECT_EQ(ParseNumber<U32>("b800h"), 0xB800u);
  EXPECT_EQ(ParseNumber<U32>("1FH"), 0x1Fu);
  EXPECT_EQ(ParseNumber<U32>("0b1010"), 0b1010u);
  // ... but under a hex reading, `0b1010` is an address
  EXPECT_EQ(ParseNumber<U32>("0b1010", Radix::HEX), 0x0B1010u);
}

TEST(ParseNumber, WhitespaceIsRejectedOnEitherSide)
{
  EXPECT_EQ(ParseNumber<U32>(" 1f", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("1f ", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>(" 12 "), std::nullopt);
}

TEST(ParseNumber, ASignOutsideAMarkerIsRefused)
{
  EXPECT_EQ(ParseNumber<S32>("-0x1f"), std::nullopt);
  EXPECT_EQ(ParseNumber<S32>("-1fh"), std::nullopt);
  // after the marker as well as in front of it
  EXPECT_EQ(ParseNumber<S32>("0x-1"), std::nullopt);
  EXPECT_EQ(ParseNumber<S32>("0x+1"), std::nullopt);
  EXPECT_EQ(ParseNumber<S32>("0b-1"), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("-1f", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("+1f", Radix::HEX), std::nullopt);
  // the digits' own sign is from_chars' rule, unchanged
  EXPECT_EQ(ParseNumber<S32>("-1f", Radix::HEX), -31);
  EXPECT_EQ(ParseNumber<S32>("-42"), -42);
  // '+' is refused at every base and target: from_chars' rule, not this file's
  EXPECT_EQ(ParseNumber<S32>("+42"), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("+42"), std::nullopt);
}

TEST(ParseNumber, EmptyAndABareMarkerAreRejected)
{
  EXPECT_EQ(ParseNumber<U32>(""), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("0x"), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("0X"), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("0b"), std::nullopt);
}

TEST(ParseNumber, TheFullRangeOfEveryWidthFits)
{
  EXPECT_EQ(ParseNumber<U08>("ff", Radix::HEX), 0xFFu);
  EXPECT_EQ(ParseNumber<U16>("ffff", Radix::HEX), 0xFFFFu);
  EXPECT_EQ(ParseNumber<U32>("ffffffff", Radix::HEX), 0xFFFFFFFFu);
  EXPECT_EQ(ParseNumber<U64>("0xFFFFFFFFFFFFFFFF"), 0xFFFFFFFFFFFFFFFFu);
}

TEST(ParseNumber, PastTheTargetTypeIsARejectionNotAWrap)
{
  // every width refuses its own overflow
  EXPECT_EQ(ParseNumber<U08>("100", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U16>("10000", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("100000000", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("ffffffffff", Radix::HEX), std::nullopt);
  EXPECT_EQ(ParseNumber<U64>("0x1FFFFFFFFFFFFFFFF"), std::nullopt);
}

TEST(ParseNumber, AsWrittenIsTheGoToBoxRule)
{
  EXPECT_EQ(ParseNumber<U64>("0x1A0", AsWritten), 0x1A0u);
  EXPECT_EQ(ParseNumber<U64>("1A0", AsWritten), 0x1A0u);
  EXPECT_EQ(ParseNumber<U64>("1a0", AsWritten), 0x1A0u);
  EXPECT_EQ(ParseNumber<U64>("100", AsWritten), 100u);
  EXPECT_EQ(ParseNumber<U64>("0", AsWritten), 0u);
  EXPECT_EQ(ParseNumber<U64>("1g", AsWritten), std::nullopt);
  EXPECT_EQ(ParseNumber<U64>("", AsWritten), std::nullopt);
  EXPECT_EQ(ParseNumber<U64>(" 100", AsWritten), std::nullopt);
}

// 'b' is a hex letter, so a heuristic that read the letters first answered
// "hex" about a token whose 'b' was the marker's own.
TEST(ParseNumber, AsWrittenHonoursTheMarkerItDocuments)
{
  EXPECT_EQ(ParseNumber<U32>("0b1010", AsWritten), 0b1010u);
  EXPECT_EQ(ParseNumber<U32>("0b101", AsWritten), 0b101u);
  EXPECT_EQ(ParseNumber<U32>("0x1A0", AsWritten), 0x1A0u);
  EXPECT_EQ(ParseNumber<U32>("0o17", AsWritten), 15u);
  EXPECT_EQ(ParseNumber<U32>("1A0h", AsWritten), 0x1A0u);
  // a bare marker is still not a number, whoever is asking
  EXPECT_EQ(ParseNumber<U32>("0b", AsWritten), std::nullopt);
  EXPECT_EQ(ParseNumber<U32>("0x", AsWritten), std::nullopt);
  // a token that names no base is still the letters' to decide
  EXPECT_EQ(ParseNumber<U32>("0755", AsWritten), 755u);
  EXPECT_EQ(ParseNumber<U32>("1a0", AsWritten), 0x1A0u);
}

static_assert(detail::number_text::WholeNumber<int>("42") == 42);
static_assert(detail::number_text::WholeNumber<int>(std::string_view{ }) == std::nullopt);
static_assert(ParseNumber<int>("0x2a") == 42);
static_assert(ParseNumber<int>("2a", AsWritten) == 42);
static_assert(ParseNumber<int>("0x-1") == std::nullopt);
static_assert(ParseNumbers<int, 2>("640x480", 'x') == std::array{ 640, 480 });
static_assert(ParseNumberAfter<int>("rate= 60 hz", "rate=") == 60);
static_assert(ParseNumberAfter<int>("rate=60", "") == std::nullopt);

TEST(ParseNumbers, ReadsExactlyTheRequestedFields)
{
  EXPECT_EQ((ParseNumbers<int, 2>("640x480", 'x')), (std::array{ 640, 480 }));
  EXPECT_EQ((ParseNumbers<int, 2>("16:9", ':')), (std::array{ 16, 9 }));
  EXPECT_EQ((ParseNumbers<int, 2>("12 34", ' ')), (std::array{ 12, 34 }));
  EXPECT_EQ((ParseNumbers<int, 2>("ff:10", ':', Radix::HEX)), (std::array{ 255, 16 }));
  EXPECT_EQ((ParseNumbers<int, 2>("0x10:0b11", ':')), (std::array{ 16, 3 }));
}

TEST(ParseNumbers, RejectsEmptyMissingExtraAndInvalidFields)
{
  for (auto const text : { "640x", "x480", "640x480x3", "640", "", "x", "640xx480",
                           "abcx480", "640xabc", "640x480junk", "640x 480" })
    EXPECT_EQ((ParseNumbers<int, 2>(text, 'x')), std::nullopt) << text;
  EXPECT_EQ((ParseNumbers<U08, 2>("256:1", ':')), std::nullopt);
}

TEST(ParseNumbers, SupportsZeroAndOneField)
{
  EXPECT_EQ((ParseNumbers<int, 0>("", ':')), (std::array<int, 0>{ }));
  EXPECT_EQ((ParseNumbers<int, 0>("1", ':')), std::nullopt);
  EXPECT_EQ((ParseNumbers<int, 1>("42", ':')), (std::array{ 42 }));
  EXPECT_EQ((ParseNumbers<int, 1>("", ':')), std::nullopt);
  EXPECT_EQ((ParseNumbers<int, 1>("42:", ':')), std::nullopt);
}

TEST(ParseNumbers, PreservesSignedLimitsAndRejectsOverflow)
{
  EXPECT_EQ((ParseNumbers<int, 2>("-1:-2", ':')), (std::array{ -1, -2 }));
  EXPECT_EQ((ParseNumbers<S08, 2>("-128:127", ':')),
            (std::array{ std::numeric_limits<S08>::min(), std::numeric_limits<S08>::max() }));
  EXPECT_EQ((ParseNumbers<S08, 2>("-129:127", ':')), std::nullopt);
  // Separators delimit fields before radix markers are interpreted.
  EXPECT_EQ((ParseNumbers<int, 2>("0x10x0x20", 'x')), std::nullopt);
}

TEST(ParseNumberAfter, ReadsTheFirstMarkedToken)
{
  EXPECT_EQ(ParseNumberAfter<int>("rate=60 hz", "rate="), 60);
  EXPECT_EQ(ParseNumberAfter<int>("t: -5", "t: "), -5);
  EXPECT_EQ(ParseNumberAfter<int>("rate=60 rate=70", "rate="), 60);
  EXPECT_EQ(ParseNumberAfter<int>("rate= \t60\r\nhz", "rate="), 60);
  EXPECT_EQ(ParseNumberAfter<int>("rate=ff", "rate=", Radix::HEX), 255);
  EXPECT_EQ(ParseNumberAfter<int>("rate=0b11", "rate="), 3);
}

TEST(ParseNumberAfter, RejectsAbsentEmptyAndInvalidTokens)
{
  for (auto const line : { "60 hz", "rate=", "rate= \t", "rate=abc", "rate=60hz",
                           "rate=abc rate=60", "" })
    EXPECT_EQ(ParseNumberAfter<int>(line, "rate="), std::nullopt) << line;
  EXPECT_EQ(ParseNumberAfter<U08>("rate=256", "rate="), std::nullopt);
  EXPECT_EQ(ParseNumberAfter<int>("rate=60", ""), std::nullopt);
}

// ---- formatting ------------------------------------------------------

TEST(HexText, IsTheTypesOwnWidthByDefault)
{
  EXPECT_EQ(HexText(U08{ 0x0Fu }), "0F");
  EXPECT_EQ(HexText(U16{ 0x1A0u }), "01A0");
  EXPECT_EQ(HexText(U32{ 0xDEADBEEFu }), "DEADBEEF");
  EXPECT_EQ(HexText(U64{ 1u }), "0000000000000001");
}

TEST(HexText, TakesAWidthAndACaseAndAPrefix)
{
  EXPECT_EQ(HexText(U32{ 0x1A0u }, 3u), "1A0");
  EXPECT_EQ(HexText(U32{ 0x1A0u }, 6u), "0001A0");
  EXPECT_EQ(HexText(U32{ 0x1A0u }, 6u, HexCase::LOWER), "0001a0");
  EXPECT_EQ(HexText(U32{ 0x1A0u }, 4u, HexCase::UPPER, "0x"), "0x01A0");
}

TEST(HexText, TheWidthIsAMinimumAndNeverATruncation)
{
  EXPECT_EQ(HexText(U64{ 0x1234567890u }, 4u), "1234567890");
  EXPECT_EQ(HexText(U32{ 0u }, 0u), "0");
}

TEST(HexText, ASignedValueIsItsBitPattern)
{
  // a hex view that said "-1" would be a hex view of nothing on the machine
  EXPECT_EQ(HexText(S32{ -1 }), "FFFFFFFF");
  EXPECT_EQ(HexText(S08{ -1 }), "FF");
  EXPECT_EQ(HexText(S32{ 16 }), "00000010");
}

TEST(FormatNumber, WritesEveryRadixItNames)
{
  EXPECT_EQ(FormatNumber(U32{ 10u }, Radix::BINARY), "1010");
  EXPECT_EQ(FormatNumber(U32{ 493u }, Radix::OCTAL), "755");
  EXPECT_EQ(FormatNumber(U32{ 493u }, Radix::DECIMAL), "493");
  EXPECT_EQ(FormatNumber(U32{ 493u }, Radix::HEX), "1ED");
  EXPECT_EQ(FormatNumber(U32{ 0u }, Radix::BINARY), "0");
}

TEST(FormatNumber, DecimalIsTheOneRadixThatWritesASign)
{
  EXPECT_EQ(FormatNumber(S32{ -493 }, Radix::DECIMAL), "-493");
  EXPECT_EQ(FormatNumber(S32{ -1 }, Radix::HEX), "FFFFFFFF");
  EXPECT_EQ(FormatNumber(S32{ -1 }, Radix::BINARY).size(), 32u);
  // the most negative value, where negating in the signed type is undefined
  EXPECT_EQ(FormatNumber(S32{ -2147483647 - 1 }, Radix::DECIMAL), "-2147483648");
  EXPECT_EQ(FormatNumber(S08{ -128 }, Radix::DECIMAL), "-128");
}

TEST(FormatNumber, PadsTheDigitsAndNotThePrefixOrTheSign)
{
  // the digits line up and the decoration does not eat into them
  EXPECT_EQ(FormatNumber(U32{ 0x1Fu }, Radix::HEX, 4u, "0x"), "0x001F");
  EXPECT_EQ(FormatNumber(S32{ -7 }, Radix::DECIMAL, 3u), "-007");
  EXPECT_EQ(FormatNumber(S32{ -7 }, Radix::DECIMAL, 3u, "#"), "-#007");
}

TEST(NumberText, TheTwoDirectionsAgree)
{
  for (U32 const value : { 0u, 1u, 0x1Fu, 0xDEADBEEFu, 0xFFFFFFFFu })
    for (auto const radix : { Radix::BINARY, Radix::OCTAL,
                              Radix::DECIMAL, Radix::HEX })
      EXPECT_EQ(ParseNumber<U32>(FormatNumber(value, radix), radix), value)
        << value << " at base " << static_cast<int>(radix);

  // and through the marker, which is the road a person's text takes
  for (U32 const value : { 0u, 0x1Fu, 0xDEADBEEFu })
    EXPECT_EQ(ParseNumber<U32>(FormatNumber(value, Radix::HEX, 0u, "0x")), value);
}
