#include "oxbox/utilities/utf-decode.hpp"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>

using namespace oxbox::utilities;

namespace
{
  // a bitfield cannot bind to gtest's `const&` parameters
  constexpr auto StepOf(UtfDecodeState const& state) noexcept -> std::uint32_t { return state.step; }

  // A malformed unit answers an engaged optional holding INVALID_CODEPOINT;
  // nullopt means only "not enough units yet".
  auto IsRejection(std::optional<char32_t> const& answer) noexcept -> bool
  { return answer.has_value() && (*answer == INVALID_CODEPOINT<char32_t>); }

  struct Answered
  {
    std::optional<char32_t> answer;
    std::size_t             at;      // index of the unit that spoke
  };

  // stops at the first unit that answers, so an early answer is not papered over
  template <std::integral _Unit, std::integral... _Units>
  auto FeedUntilAnswer(_Units... units) -> Answered
  {
    auto const feed{ std::array<std::uint32_t, sizeof...(_Units)>{
      static_cast<std::uint32_t>(units)... } };
    UtfDecodeState state{ };
    for (auto index{ std::size_t{ 0u } }; index < feed.size(); ++index) {
      auto const answer{ UtfDecode<char32_t>(state,
                           static_cast<_Unit>(feed[index])) };
      if (answer.has_value()) { return { answer, index }; } }
    return { std::nullopt, feed.size() };
  }

  template <std::integral _Unit, std::integral... _Units>
  auto ExpectRejectedAt(std::size_t where, _Units... units) -> void
  {
    auto const [answer, at]{ FeedUntilAnswer<_Unit>(units...) };
    auto const label{ std::format("expected a rejection at unit {}", where) };
    ASSERT_TRUE(answer.has_value()) << label << ": nothing answered at all";
    EXPECT_TRUE(IsRejection(answer)) << label << ": answered "
      << std::format("U+{:04X}", std::uint32_t{ *answer });
    EXPECT_EQ(at, where) << label << ": a different unit answered";
  }

  template <std::integral _Unit, std::integral... _Units>
  auto ExpectDecodesOnLastUnit(std::uint32_t codepoint, _Units... units) -> void
  {
    constexpr auto WIDTH{ sizeof...(_Units) };
    auto const feed { std::array<std::uint32_t, WIDTH>{ static_cast<std::uint32_t>(units)... } };
    auto const label{ std::format("U+{:04X} from {} unit(s)", codepoint, WIDTH) };
    UtfDecodeState state{ };
    for (auto const unit : feed | std::views::take(WIDTH - 1u)) {
      EXPECT_FALSE(UtfDecode<char32_t>(state, static_cast<_Unit>(unit)).has_value())
        << label << ": completed early";
      }
    auto const answer{ UtfDecode<char32_t>(state, static_cast<_Unit>(feed.back())) };
    ASSERT_TRUE(answer.has_value()) << label << ": never completed";
    EXPECT_FALSE(IsRejection(answer)) << label << ": rejected, not decoded";
    EXPECT_EQ(std::uint32_t{ *answer }, codepoint) << label;
    EXPECT_EQ(StepOf(state), 0u) << label << ": state not drained";
  }
}

TEST(UtfDecodeUtf8, AsciiDecodesOnTheFirstOctet)
{
  ExpectDecodesOnLastUnit<char8_t>(0x41u, 0x41u);
  ExpectDecodesOnLastUnit<char8_t>(0x00u, 0x00u);
  ExpectDecodesOnLastUnit<char8_t>(0x7Fu, 0x7Fu);
}

TEST(UtfDecodeUtf8, EveryWidthCompletesExactlyOnItsLastOctet)
{
  ExpectDecodesOnLastUnit<char8_t>(0x000080u, 0xC2u, 0x80u);
  ExpectDecodesOnLastUnit<char8_t>(0x0007FFu, 0xDFu, 0xBFu);
  ExpectDecodesOnLastUnit<char8_t>(0x000800u, 0xE0u, 0xA0u, 0x80u);
  ExpectDecodesOnLastUnit<char8_t>(0x00FFFFu, 0xEFu, 0xBFu, 0xBFu);
  ExpectDecodesOnLastUnit<char8_t>(0x010000u, 0xF0u, 0x90u, 0x80u, 0x80u);
  ExpectDecodesOnLastUnit<char8_t>(0x01F600u, 0xF0u, 0x9Fu, 0x98u, 0x80u);
  ExpectDecodesOnLastUnit<char8_t>(0x10FFFFu, 0xF4u, 0x8Fu, 0xBFu, 0xBFu);
}

TEST(UtfDecodeUtf8, AStrayContinuationOctetFailsImmediately)
{
  ExpectRejectedAt<char8_t>(0u, 0x80u);
}

TEST(UtfDecodeUtf8, OctetsThatAreNoLeadAtAllFail)
{
  // 0xFF is the one that matters: eight leading ones would wrap the three-bit
  // step field to zero and read back as ASCII
  ExpectRejectedAt<char8_t>(0u, 0xF8u);
  ExpectRejectedAt<char8_t>(0u, 0xFCu);
  ExpectRejectedAt<char8_t>(0u, 0xFEu);
  ExpectRejectedAt<char8_t>(0u, 0xFFu);
}

TEST(UtfDecodeUtf8, ALeadFollowedByANonContinuationFailsAndConsumesTheFollowByte)
{
  ExpectRejectedAt<char8_t>(1u, 0xC2u, 0x41u);

  ExpectRejectedAt<char8_t>(1u, 0xE0u, 0xC2u);

  UtfDecodeState state{ };
  EXPECT_FALSE(UtfDecode<char32_t>(state, char8_t{ 0xC2u }).has_value());
  EXPECT_TRUE(IsRejection(UtfDecode<char32_t>(state, char8_t{ 0x41u })));
  EXPECT_EQ(StepOf(state), 0u);
}

TEST(UtfDecodeUtf8, DecodingResumesAfterARejection)
{
  UtfDecodeState state{ };
  EXPECT_TRUE(IsRejection(UtfDecode<char32_t>(state, char8_t{ 0x80u })));
  EXPECT_EQ(StepOf(state), 0u);

  auto const ascii{ UtfDecode<char32_t>(state, char8_t{ 0x41u }) };
  ASSERT_TRUE(ascii.has_value());
  EXPECT_EQ(std::uint32_t{ *ascii }, 0x41u);

  ExpectDecodesOnLastUnit<char8_t>(0x0007FFu, 0xDFu, 0xBFu);
}

TEST(UtfDecodeUtf8, OverlongSequencesAreRejected)
{
  // C0 80 is the classic hole: an overlong U+0000 that walks a NUL past a
  // filter watching for the single-octet form
  ExpectRejectedAt<char8_t>(0u, 0xC0u, 0x80u);               // C0/C1 lead nothing
  ExpectRejectedAt<char8_t>(0u, 0xC0u, 0xAFu);
  ExpectRejectedAt<char8_t>(1u, 0xE0u, 0x81u, 0x81u);        // E0's floor
  ExpectRejectedAt<char8_t>(1u, 0xF0u, 0x80u, 0xA0u, 0x80u); // F0's floor

  // a value past the last codepoint can only be judged once it is complete
  ExpectRejectedAt<char8_t>(3u, 0xF7u, 0xBFu, 0xBFu, 0xBFu);
  ExpectRejectedAt<char8_t>(3u, 0xF4u, 0x90u, 0x80u, 0x80u);
}

TEST(UtfDecodeUtf8, TheFloorsAdmitTheirSmallestLegalSecondByte)
{
  // E0 A0 and F0 90 sit exactly on Unicode table 3-7's floors
  ExpectDecodesOnLastUnit<char8_t>(0x000800u, 0xE0u, 0xA0u, 0x80u);
  ExpectDecodesOnLastUnit<char8_t>(0x010000u, 0xF0u, 0x90u, 0x80u, 0x80u);
}

TEST(UtfDecodeUtf8, ThreeOctetSurrogatesAreRejected)
{
  // ED A0 80 is a high surrogate smuggled through UTF-8: CESU-8
  ExpectRejectedAt<char8_t>(2u, 0xEDu, 0xA0u, 0x80u);
  ExpectRejectedAt<char8_t>(2u, 0xEDu, 0xBFu, 0xBFu);
}

TEST(UtfDecodeUtf16, BmpUnitsDecodeOnTheUnitThatCarriesThem)
{
  ExpectDecodesOnLastUnit<char16_t>(0x0041u, 0x0041u);
  ExpectDecodesOnLastUnit<char16_t>(0x07FFu, 0x07FFu);
  ExpectDecodesOnLastUnit<char16_t>(0xFFFDu, 0xFFFDu);
  ExpectDecodesOnLastUnit<char16_t>(0xFFFFu, 0xFFFFu);
}

TEST(UtfDecodeUtf16, AByteOrderMarkIsJustACharacterAtThisLayer)
{
  ExpectDecodesOnLastUnit<char16_t>(0xFEFFu, 0xFEFFu);
}

TEST(UtfDecodeUtf16, SurrogatePairsCompleteOnTheLowUnit)
{
  ExpectDecodesOnLastUnit<char16_t>(0x010000u, 0xD800u, 0xDC00u);
  ExpectDecodesOnLastUnit<char16_t>(0x01F600u, 0xD83Du, 0xDE00u);
  ExpectDecodesOnLastUnit<char16_t>(0x10FFFFu, 0xDBFFu, 0xDFFFu);
}

TEST(UtfDecodeUtf16, ALoneLowSurrogateFails)
{
  ExpectRejectedAt<char16_t>(0u, 0xDC00u);
}

TEST(UtfDecodeUtf16, AHighSurrogateFollowedByAnotherHighSurrogateFails)
{
  ExpectRejectedAt<char16_t>(1u, 0xD83Du, 0xD83Du);

  UtfDecodeState state{ };
  EXPECT_FALSE(UtfDecode<char32_t>(state, char16_t{ 0xD83Du }).has_value());
  EXPECT_TRUE(IsRejection(UtfDecode<char32_t>(state, char16_t{ 0xD83Du })));
  EXPECT_EQ(StepOf(state), 0u);
  EXPECT_TRUE(IsRejection(UtfDecode<char32_t>(state, char16_t{ 0xDE00u })));
}

TEST(UtfDecodeUtf16, AHighSurrogateFollowedByABmpUnitFailsAndConsumesTheUnit)
{
  ExpectRejectedAt<char16_t>(1u, 0xD83Du, 0x0041u);
}

TEST(UtfDecodeUtf32, ValidCodepointsPassThroughUnchanged)
{
  ExpectDecodesOnLastUnit<char32_t>(0x000000u, 0x000000u);
  ExpectDecodesOnLastUnit<char32_t>(0x000041u, 0x000041u);
  ExpectDecodesOnLastUnit<char32_t>(0x00FFFFu, 0x00FFFFu);
  ExpectDecodesOnLastUnit<char32_t>(0x010000u, 0x010000u);
  ExpectDecodesOnLastUnit<char32_t>(0x10FFFFu, 0x10FFFFu);
}

TEST(UtfDecodeUtf32, SurrogateValuesAndOutOfRangeValuesFail)
{
  ExpectRejectedAt<char32_t>(0u, 0x00D800u);
  ExpectRejectedAt<char32_t>(0u, 0x00DC00u);
  ExpectRejectedAt<char32_t>(0u, 0x00DFFFu);
  ExpectRejectedAt<char32_t>(0u, 0x110000u);
  ExpectRejectedAt<char32_t>(0u, 0xFFFFFFFFu);
}

TEST(UtfDecodeMixedWidth, AStateMidUtf8HandedAUtf16UnitIsRejected)
{
  // the state carries a UTF-8 width the 16-bit decoder cannot honour
  UtfDecodeState state{ };
  ASSERT_FALSE(UtfDecode<char32_t>(state, char8_t{ 0xF0u }).has_value());
  ASSERT_EQ(StepOf(state), 4u);

  EXPECT_TRUE(IsRejection(UtfDecode<char32_t>(state, char16_t{ 0x0041u })));
  EXPECT_EQ(StepOf(state), 0u);   // and the unusable state is drained
}
