// Alignment both ways, the width-to-type maps, and the four bit-field
// operations, at the widths `(1 << length) - 1` cannot answer for.
#include "oxbox/utilities/bits.hpp"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace
{
  using namespace oxbox::utilities;
}

// ---- alignment -------------------------------------------------------

TEST(AlignUp, RoundsUpToBoundary)
{
  EXPECT_EQ(AlignUp(std::size_t{  0u }, std::size_t{ 8u }),  0u);
  EXPECT_EQ(AlignUp(std::size_t{  1u }, std::size_t{ 8u }),  8u);
  EXPECT_EQ(AlignUp(std::size_t{  8u }, std::size_t{ 8u }),  8u);
  EXPECT_EQ(AlignUp(std::size_t{  9u }, std::size_t{ 8u }), 16u);
  EXPECT_EQ(AlignUp(std::size_t{ 13u }, std::size_t{ 4u }), 16u);
}

TEST(AlignUp, AlignmentOfOneIsIdentity)
{
  EXPECT_EQ(AlignUp(std::size_t{ 7u }, std::size_t{ 1u }), 7u);
  EXPECT_EQ(AlignUp<1u>(std::size_t{ 7u }), 7u);
}

TEST(AlignUp, ABoundaryThatIsNotAPowerOfTwoStillAnswers)
{
  // the fast path is a fast path and not a precondition
  EXPECT_EQ(AlignUp(std::size_t{ 10u }, std::size_t{ 3u }), 12u);
  EXPECT_EQ(AlignUp(std::size_t{ 12u }, std::size_t{ 3u }), 12u);
  EXPECT_EQ(AlignUp(std::size_t{  1u }, std::size_t{ 100u }), 100u);
}

// the compile-time form is the same answer, folded
static_assert(AlignUp(std::size_t{ 100u }, std::size_t{ 64u }) == 128u);
static_assert(AlignUp<64u>(std::size_t{ 100u }) == 128u);
static_assert(AlignUp<8u>(std::size_t{ 0u }) == 0u);
static_assert(AlignUp<8u>(U32{ 4097u }) == 4104u);
static_assert(AlignUp<8u>(std::size_t{ 61u }) == 64u);

// a boundary that is not a power of two is refused where it is a fact about
// the code, rather than being answered slowly
template <std::size_t _BOUNDARY>
concept AlignsAtCompileTime =
  requires (std::size_t value) { AlignUp<_BOUNDARY>(value); };
static_assert( AlignsAtCompileTime<64u>);
static_assert(!AlignsAtCompileTime<3u>);

TEST(IsAligned, AnswersBothSpellings)
{
  EXPECT_TRUE (IsAligned(std::size_t{ 64u }, std::size_t{ 8u }));
  EXPECT_FALSE(IsAligned(std::size_t{ 65u }, std::size_t{ 8u }));
  EXPECT_TRUE (IsAligned<8u>(std::size_t{ 64u }));
  EXPECT_FALSE(IsAligned<8u>(std::size_t{ 65u }));
  EXPECT_TRUE (IsAligned(std::size_t{ 12u }, std::size_t{ 3u }));
}

TEST(AlignUpAs, TakesTheBoundaryFromAType)
{
  EXPECT_EQ(AlignUpAs<U64>(std::size_t{ 1u }), alignof(U64));
  EXPECT_EQ(AlignUpAs<U08>(std::size_t{ 7u }), 7u);
}

// ---- widening --------------------------------------------------------

TEST(IntExtend, SignExtendsSignedSourceAndZeroExtendsUnsigned)
{
  EXPECT_EQ((IntExtend<S32, S08>(static_cast<S08>(0xFFu))),   -1);
  EXPECT_EQ((IntExtend<S32, S08>(static_cast<S08>(0xFBu))),   -5);
  EXPECT_EQ((IntExtend<S32, S16>(static_cast<S16>(0x1234u))), 0x1234);
  EXPECT_EQ((IntExtend<U32, U08>(U08{ 0xFFu })),   0xFFu);
  EXPECT_EQ((IntExtend<U32, U16>(U16{ 0xFFFFu })), 0xFFFFu);
}

// mixing the two is what the requires clause exists to stop: the answers
// differ by 0xFFFF0000
template <typename _Dst, typename _Src>
concept Extends = requires (_Src value) { IntExtend<_Dst>(value); };
static_assert( Extends<S32, S08>);
static_assert( Extends<U32, U08>);
static_assert(!Extends<U32, S08>);   // sign-ness must match
static_assert(!Extends<S08, S32>);   // and it only ever widens

// ---- the width-to-type maps -----------------------------------------

static_assert(std::same_as<UIntOfSize<1u>, U08>);
static_assert(std::same_as<UIntOfSize<8u>, U64>);
static_assert(std::same_as<SIntOfSize<2u>, S16>);
static_assert(std::same_as<UIntOfLength<32u>, U32>);
static_assert(std::same_as<SIntOfLength<64u>, S64>);
static_assert(std::same_as<UIntOfAtLeastLength<5u>, U08>);
static_assert(std::same_as<UIntOfAtLeastLength<9u>, U16>);
static_assert(std::same_as<UIntOfAtLeastLength<33u>, U64>);
static_assert(std::same_as<UIntOfAtLeastSize<3u>, U32>);
static_assert(std::same_as<SIntOfAtLeastLength<12u>, S16>);
static_assert(IntSizeValid(4u) && !IntSizeValid(3u) && !IntSizeValid(16u));
static_assert(IntLengthValid(16u) && !IntLengthValid(12u));

// ---- the masks -------------------------------------------------------

TEST(LowBits, AnswersAtTheTypesOwnWidthToo)
{
  // `(1 << length) - 1` is undefined exactly here
  EXPECT_EQ(LowBits<U08>(0u), 0x00u);
  EXPECT_EQ(LowBits<U08>(4u), 0x0Fu);
  EXPECT_EQ(LowBits<U08>(8u), 0xFFu);
  EXPECT_EQ(LowBits<U32>(32u), 0xFFFFFFFFu);
  EXPECT_EQ(LowBits<U64>(64u), 0xFFFFFFFFFFFFFFFFu);
  EXPECT_EQ(LowBits<U08>(99u), 0xFFu);
}

// ---- extract / replace -----------------------------------------------

TEST(ExtractBits, ReadsTheModRmFields)
{
  // 0xC3 = 0b11_000_011 -- mod bits 7:6, reg bits 5:3, rm bits 2:0
  EXPECT_EQ(ExtractBits(U08{ 0xC3u }, 6u, 2u), 0b11u);
  EXPECT_EQ(ExtractBits(U08{ 0xC3u }, 3u, 3u), 0b000u);
  EXPECT_EQ(ExtractBits(U08{ 0xC3u }, 0u, 3u), 0b011u);
}

TEST(ExtractBits, FullWidthPassesThroughAndZeroLengthIsEmpty)
{
  EXPECT_EQ(ExtractBits(U08{ 0xABu }, 0u, 8u), 0xABu);
  EXPECT_EQ(ExtractBits(U32{ 0xDEADBEEFu }, 0u, 32u), 0xDEADBEEFu);
  EXPECT_EQ(ExtractBits(U08{ 0xFFu }, 3u, 0u), 0u);
  EXPECT_EQ(ExtractBits(U16{ 0xDEADu }, 12u, 4u), 0xDu);
}

TEST(ExtractBits, AFieldOffTheTopIsWhatIsThereAndNotUndefined)
{
  EXPECT_EQ(ExtractBits(U08{ 0xFFu }, 6u, 8u), 0b11u);   // clipped, not shifted off
  EXPECT_EQ(ExtractBits(U08{ 0xFFu }, 8u, 1u), 0u);      // an offset past the top
  EXPECT_EQ(ExtractBits(U08{ 0xFFu }, 99u, 4u), 0u);
}

TEST(ReplaceBits, WritesTheFieldAndNothingElse)
{
  // clear bits 5:3 of 0xC3 and write 0b101 -> 0b11_101_011 = 0xEB
  EXPECT_EQ(ReplaceBits(U08{ 0xC3u }, 3u, 3u, U08{ 0b101u }), 0xEBu);
  // an over-wide field is masked to its width rather than spilling
  EXPECT_EQ(ReplaceBits(U08{ 0x00u }, 3u, 3u, U08{ 0xFFu }), 0b00111000u);
  // and an offset past the top changes nothing at all
  EXPECT_EQ(ReplaceBits(U08{ 0xC3u }, 8u, 3u, U08{ 0b101u }), 0xC3u);
}

TEST(ReplaceBits, RoundTripsWithExtractBits)
{
  constexpr U16 VALUE{ 0xDEADu };
  auto const replaced{ ReplaceBits(VALUE, 4u, 4u, U16{ 0x7u }) };
  EXPECT_EQ(ExtractBits(replaced, 4u, 4u), 0x7u);
  EXPECT_EQ(ExtractBits(replaced, 0u, 4u), ExtractBits(VALUE, 0u, 4u));
  EXPECT_EQ(ExtractBits(replaced, 12u, 4u), ExtractBits(VALUE, 12u, 4u));
}

// ---- explode / compact ------------------------------------------------

TEST(ExplodeBits, SplitsAWordIntoItsNamedFields)
{
  // 0xC3 = 0b11_000_011 -> mod=3, reg=0, rm=3
  constexpr auto PARTS{ ExplodeBits<2, 3, 3>(U08{ 0xC3u }) };
  EXPECT_EQ(PARTS[0], 0b11u);
  EXPECT_EQ(PARTS[1], 0b000u);
  EXPECT_EQ(PARTS[2], 0b011u);

  constexpr auto MAXED{ ExplodeBits<2, 3, 3>(U08{ 0xFFu }) };
  EXPECT_EQ(MAXED[0], 0b11u);
  EXPECT_EQ(MAXED[1], 0b111u);
  EXPECT_EQ(MAXED[2], 0b111u);
}

TEST(ExplodeBits, OneFieldIsThatFieldAndAPartialWidthIsLowAligned)
{
  EXPECT_EQ((ExplodeBits<8>(U08{ 0xABu }))[0], 0xABu);
  EXPECT_EQ((ExplodeBits<4>(U08{ 0xABu }))[0], 0xBu);
  EXPECT_EQ((ExplodeBits<32>(U32{ 0xDEADBEEFu }))[0], 0xDEADBEEFu);
}

TEST(ExplodeBits, ReadsThroughAStructuredBinding)
{
  auto const [d, e, a, second_d]{ ExplodeBits<4, 4, 4, 4>(U16{ 0xDEADu }) };
  EXPECT_EQ(d, 0xDu);
  EXPECT_EQ(e, 0xEu);
  EXPECT_EQ(a, 0xAu);
  EXPECT_EQ(second_d, 0xDu);

  auto const [b0, b1, b2, b3]{ ExplodeBits<8, 8, 8, 8>(U32{ 0xDEADBEEFu }) };
  EXPECT_EQ(b0, 0xDEu);
  EXPECT_EQ(b1, 0xADu);
  EXPECT_EQ(b2, 0xBEu);
  EXPECT_EQ(b3, 0xEFu);
}

TEST(CompactBits, PacksFieldsMostSignificantFirst)
{
  EXPECT_EQ((CompactBits<2, 3, 3>(U08{ 0b11u }, U08{ 0b000u }, U08{ 0b011u })),
            0xC3u);
  EXPECT_EQ((CompactBits<2, 3, 3>(U08{ 0b11u }, U08{ 0b111u }, U08{ 0b111u })),
            0xFFu);
  EXPECT_EQ((CompactBits<4, 4, 4, 4>(U16{ 0xDu }, U16{ 0xEu },
                                     U16{ 0xAu }, U16{ 0xDu })),
            0xDEADu);
}

TEST(CompactBits, MasksEachFieldToItsWidth)
{
  EXPECT_EQ((CompactBits<2, 3, 3>(U32{ 0xFFu }, U32{ 0xFFu }, U32{ 0xFFu })),
            0xFFu);
}

TEST(CompactBits, RoundTripsWithExplodeBits)
{
  auto const [mod, reg, rm]{ ExplodeBits<2, 3, 3>(U08{ 0xC3u }) };
  EXPECT_EQ((CompactBits<2, 3, 3>(mod, reg, rm)), 0xC3u);
  EXPECT_EQ((CompactBits<32>(U32{ 0xDEADBEEFu })), 0xDEADBEEFu);
}

// field widths that do not add up to a whole type still have somewhere to live
static_assert(std::same_as<decltype(CompactBits<2, 3>(U08{ }, U08{ })), U08>);
static_assert(std::same_as<decltype(CompactBits<8, 8, 8>(U32{ }, U32{ }, U32{ })), U32>);
static_assert(CompactBits<2, 3>(U08{ 0b10u }, U08{ 0b011u }) == 0b10011u);
