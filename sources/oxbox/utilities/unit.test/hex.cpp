// Both directions of the hex grammar, and the compile-time literals that
// turn a typo in a fixture into a build failure.
#include "oxbox/utilities/hex.hpp"

#include "oxbox/utilities/unit.test/octets.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
  using namespace oxbox::utilities;
  using namespace oxbox::utilities::literals;
  using oxbox::utilities::test::Octets;
  using namespace std::string_view_literals;

  using ByteVector = std::vector<std::byte>;

  constexpr auto SAMPLE{ Octets(0x0F, 0xA0, 0x12) };

  auto Sample() noexcept -> Bytes { return SAMPLE; }

  auto Vector(std::initializer_list<int> octets) -> ByteVector
  {
    ByteVector out;
    for (int const octet : octets)
      out.push_back(std::byte{ static_cast<U08>(octet) });
    return out;
  }
}

// ---- the nibble table -----------------------------------------------

static_assert(HexDigitValue('0') == 0u);
static_assert(HexDigitValue('9') == 9u);
static_assert(HexDigitValue('a') == 10u);
static_assert(HexDigitValue('F') == 15u);
static_assert(HexDigitValue('g') == std::nullopt);
static_assert(HexDigitValue(' ') == std::nullopt);
static_assert(IsHexDigit('B') && !IsHexDigit('x'));

// char may be signed here, so the comparisons must not wrap a negative char
// into a digit range. Spelled through the octet: '\x80' as a char literal is
// implementation-defined in sign.
TEST(HexDigitValue, ABytePastAsciiIsNotADigitWhicheverWayCharIsSigned)
{
  for (int octet{ 0x80 }; octet <= 0xFF; ++octet)
    EXPECT_EQ(HexDigitValue(static_cast<char>(octet)), std::nullopt)
      << "octet " << octet;
  EXPECT_FALSE(IsHexDigit(static_cast<char>(0x80)));
  EXPECT_FALSE(IsHexDigit(static_cast<char>(0xE0)));
  auto const fault{ HexFaultIn("4d\xC3\xA9") };
  ASSERT_TRUE(fault.has_value());
  EXPECT_EQ(fault->kind, HexFault::Kind::STRAY_CHARACTER);
  EXPECT_EQ(fault->position, 2u);
}

// ---- bytes -> text ---------------------------------------------------

TEST(ToHex, WritesLowercaseByDefault)
{
  EXPECT_EQ(ToHex(Sample()), "0fa012");
}

TEST(ToHex, WritesUppercaseWhenAsked)
{
  EXPECT_EQ(ToHex(Sample(), HexCase::UPPER), "0FA012");
}

TEST(ToHex, PutsTheSeparatorBetweenBytesAndNowhereElse)
{
  EXPECT_EQ(ToHex(Sample(), HexCase::LOWER, " "sv), "0f a0 12");
  EXPECT_EQ(ToHex(Sample(), HexCase::UPPER, ", "sv), "0F, A0, 12");
  EXPECT_EQ(ToHex(Sample().first(1u), HexCase::LOWER, "-"sv), "0f");
}

TEST(ToHex, EmptyIsEmptyWhateverItIsAsked)
{
  EXPECT_EQ(ToHex(Bytes{ }), "");
  EXPECT_EQ(ToHex(Bytes{ }, HexCase::UPPER, ", "sv), "");
}

// the encode side is constant-evaluable end to end
static_assert(ToHex(Bytes{ SAMPLE }) == "0fa012");
static_assert(ToHex(Bytes{ SAMPLE }, HexCase::UPPER, ":"sv) == "0F:A0:12");

TEST(HexTextSize, CountsTheSeparatorsThatActuallyFall)
{
  EXPECT_EQ(HexTextSize(0u), 0u);
  EXPECT_EQ(HexTextSize(0u, 1u), 0u);
  EXPECT_EQ(HexTextSize(1u, 1u), 2u);
  EXPECT_EQ(HexTextSize(3u, 1u), 8u);
  EXPECT_EQ(HexTextSize(16u), 32u);
  EXPECT_EQ(ToHex(Sample(), HexCase::LOWER, " - "sv).size(),
            HexTextSize(3u, 3u));
}

TEST(ToHexInto, FillsACallersBufferWithoutAllocating)
{
  std::array<char, 8u> row{ };
  EXPECT_EQ(ToHexInto(std::span{ row }, Sample(), HexCase::UPPER, " "sv),
            "0F A0 12");
}

TEST(ToHexInto, AShortBufferStopsOnAByteBoundaryAndSaysSo)
{
  // whole bytes, no trailing separator, and the view's size is the truth
  std::array<char, 5u> narrow{ };
  auto const written{ ToHexInto(std::span{ narrow }, Sample(), HexCase::LOWER, " "sv) };
  EXPECT_EQ(written, "0f a0");
  EXPECT_EQ(written.size(), 5u);

  std::array<char, 4u> narrower{ };
  EXPECT_EQ(ToHexInto(std::span{ narrower }, Sample(), HexCase::LOWER, " "sv), "0f");

  std::array<char, 1u> hopeless{ };
  EXPECT_TRUE(ToHexInto(std::span{ hopeless }, Sample()).empty());
}

// ---- text -> bytes ---------------------------------------------------

TEST(BytesFromHex, ReadsTheSpellingsPeopleActuallyPaste)
{
  EXPECT_EQ(BytesFromHex("4d5a"), Vector({ 0x4D, 0x5A }));
  EXPECT_EQ(BytesFromHex("4D 5A"), Vector({ 0x4D, 0x5A }));
  EXPECT_EQ(BytesFromHex("4d-5a"), Vector({ 0x4D, 0x5A }));
  EXPECT_EQ(BytesFromHex("4d,5a"), Vector({ 0x4D, 0x5A }));
  EXPECT_EQ(BytesFromHex("4d:5a"), Vector({ 0x4D, 0x5A }));
  // separators are ignored wherever they fall, not only between bytes
  EXPECT_EQ(BytesFromHex(" 4 d 5 a "), Vector({ 0x4D, 0x5A }));
}

TEST(BytesFromHex, EmptyIsAnEmptySequenceAndNotAFailure)
{
  auto const nothing{ BytesFromHex("") };
  ASSERT_TRUE(nothing.has_value());
  EXPECT_TRUE(nothing->empty());
  // separators alone are still no bytes typed
  EXPECT_TRUE(BytesFromHex("  ")->empty());
}

TEST(BytesFromHex, RefusesAStrayCharacterOrAHalfByte)
{
  EXPECT_EQ(BytesFromHex("4g"), std::nullopt);
  EXPECT_EQ(BytesFromHex("4d5"), std::nullopt);
  EXPECT_EQ(BytesFromHex("0x4d"), std::nullopt);
}

TEST(HexFaultIn, NamesTheStrayCharacterAndWhereItIs)
{
  auto const stray{ HexFaultIn("4d 5g 6a") };
  ASSERT_TRUE(stray.has_value());
  EXPECT_EQ(stray->kind, HexFault::Kind::STRAY_CHARACTER);
  EXPECT_EQ(stray->character, 'g');
  EXPECT_EQ(stray->position, 4u);      // into the text, separators and all
  EXPECT_EQ(stray->digits, 3u);        // counted up to the fault

  auto const prefix{ HexFaultIn("0x4d") };
  ASSERT_TRUE(prefix.has_value());
  EXPECT_EQ(prefix->character, 'x');
  EXPECT_EQ(prefix->position, 1u);
}

TEST(HexFaultIn, CountsTheDigitsOfAHalfByte)
{
  auto const half{ HexFaultIn("4d5") };
  ASSERT_TRUE(half.has_value());
  EXPECT_EQ(half->kind, HexFault::Kind::HALF_BYTE);
  EXPECT_EQ(half->digits, 3u);
  EXPECT_EQ(half->character, '5');     // the last digit
  EXPECT_EQ(half->position, 2u);

  auto const spaced{ HexFaultIn("4d 5a c") };
  ASSERT_TRUE(spaced.has_value());
  EXPECT_EQ(spaced->kind, HexFault::Kind::HALF_BYTE);
  EXPECT_EQ(spaced->digits, 5u);
  EXPECT_EQ(spaced->position, 6u);
}

TEST(HexFaultIn, NulloptMeansTheTextParses)
{
  EXPECT_EQ(HexFaultIn(""), std::nullopt);
  EXPECT_EQ(HexFaultIn("  "), std::nullopt);
  EXPECT_EQ(HexFaultIn("4d5a"), std::nullopt);
  EXPECT_EQ(HexFaultIn("4D 5A - 00"), std::nullopt);
  // a narrowed separator set makes a separator a stray character
  EXPECT_EQ(HexFaultIn("4d 5a", " "sv), std::nullopt);
  ASSERT_TRUE(HexFaultIn("4d,5a", " "sv).has_value());
  EXPECT_EQ(HexFaultIn("4d,5a", " "sv)->character, ',');
}

TEST(HexFaultIn, AgreesWithHexByteCountOnEveryVerdict)
{
  for (auto const& text : { ""sv, "  "sv, "4d5a"sv, "4d 5a"sv, "4g"sv,
                            "4d5"sv, "0x4d"sv, "4d 5a c"sv, "-"sv })
    EXPECT_EQ(HexFaultIn(text).has_value(), !HexByteCount(text).has_value())
      << text;
}

static_assert(!HexFaultIn("4d5a").has_value());
static_assert(HexFaultIn("4d5").value().kind == HexFault::Kind::HALF_BYTE);
static_assert(HexFaultIn("4g").value().character == 'g');

TEST(BytesFromHex, TheSeparatorSetIsTheCallersToNarrow)
{
  EXPECT_EQ(BytesFromHex("4d 5a", " "sv), Vector({ 0x4D, 0x5A }));
  EXPECT_EQ(BytesFromHex("4d,5a", " "sv), std::nullopt);
}

TEST(HexByteCount, IsTheSizeTheDecodeWillProduce)
{
  EXPECT_EQ(HexByteCount("4d5a"), 2u);
  EXPECT_EQ(HexByteCount("4d 5a - 00"), 3u);
  EXPECT_EQ(HexByteCount(""), 0u);
  EXPECT_EQ(HexByteCount("4d5"), std::nullopt);
  EXPECT_EQ(HexByteCount("4g"), std::nullopt);
}

TEST(BytesFromHexInto, WritesIntoACallersBufferAndRefusesATooSmallOne)
{
  std::array<std::byte, 4u> into{ };
  auto const written{ BytesFromHexInto(WritableBytes{ into }, "4d 5a") };
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(written->size(), 2u);
  EXPECT_EQ((*written)[0], std::byte{ 0x4Du });

  // a buffer that cannot hold the answer is a refusal
  std::array<std::byte, 1u> narrow{ };
  EXPECT_EQ(BytesFromHexInto(WritableBytes{ narrow }, "4d 5a"), std::nullopt);
  // a refusal leaves the good prefix behind: read the answer, not the array
  EXPECT_EQ(narrow[0], std::byte{ 0x4Du });
}

// the decode side is constant-evaluable too
static_assert([] {
  std::array<std::byte, 2u> into{ };
  auto const written{ BytesFromHexInto(WritableBytes{ into }, "4d5a") };
  return written && written->size() == 2u && into[1] == std::byte{ 0x5Au };
}());

TEST(HexRoundTrip, TextToBytesToTextIsTheSameText)
{
  for (auto const& text : { "00"sv, "0fa012"sv, "deadbeef"sv, ""sv })
  {
    auto const bytes{ BytesFromHex(text) };
    ASSERT_TRUE(bytes.has_value()) << text;
    EXPECT_EQ(ToHex(Bytes{ *bytes }), text);
  }
}

// ---- the literals ----------------------------------------------------

// The text form is a function and not a literal; hex.hpp says which compiler
// decided that.
static_assert(HexBytes<"4d5a">() == Octets(0x4D, 0x5A));
static_assert(HexBytes<"4D 5A">() == Octets(0x4D, 0x5A));
static_assert(HexBytes<"0f a0 12">() == SAMPLE);
static_assert(HexBytes<"">() == std::array<std::byte, 0u>{ });

static_assert(0xDEADBEEF_hex == Octets(0xDE, 0xAD, 0xBE, 0xEF));
static_assert(0x90_hex == Octets(0x90));
// an odd number of digits pads at the front: 0xABC is 0x0ABC
static_assert(0xABC_hex == Octets(0x0A, 0xBC));
// C++'s digit separator means nothing in a literal's value
static_assert(0xDEAD'BEEF_hex == Octets(0xDE, 0xAD, 0xBE, 0xEF));
static_assert(0xDEAD'BEEF_hex == 0xDEADBEEF_hex);
static_assert(0x1'2'3'4_hex == Octets(0x12, 0x34));
static_assert(1234'5678_hex == Octets(0x12, 0x34, 0x56, 0x78));

TEST(HexLiteral, TheStringAndNumericFormsAgree)
{
  EXPECT_EQ(0xDEADBEEF_hex, HexBytes<"de ad be ef">());
  EXPECT_EQ(ToHex(Bytes{ 0x90CD21_hex }, HexCase::UPPER), "90CD21");
}

TEST(HexLiteral, IsAnArrayAndNotAContainer)
{
    // the size is in the type, so a fixture costs no allocation
  constexpr auto FIXTURE{ 0x4D5A_hex };
  static_assert(FIXTURE.size() == 2u);
  EXPECT_EQ(ToHex(Bytes{ FIXTURE }), "4d5a");
}

// What must not compile is not checked here: the cases are
// tools/negative-compile.sh's `hex-literal.*` rows.
