#pragma once
// What a code point is and whether it is one: the vocabulary of widths and
// encodings, the Unicode range constants, and the validity rules over them.

#include <concepts>
#include <cstdint>

namespace oxbox::utilities::detail::codepoint
{
  using std::int8_t;
  using std::uint8_t;
  using std::uint16_t;
  using std::uint32_t;

  template <std::integral _Value> requires(sizeof(_Value) == sizeof(char8_t )) inline constexpr auto AsChar (_Value value = _Value{ 0 }) noexcept { return static_cast<char8_t >(value); }
  template <std::integral _Value> requires(sizeof(_Value) == sizeof(char16_t)) inline constexpr auto AsChar (_Value value = _Value{ 0 }) noexcept { return static_cast<char16_t>(value); }
  template <std::integral _Value> requires(sizeof(_Value) == sizeof(char32_t)) inline constexpr auto AsChar (_Value value = _Value{ 0 }) noexcept { return static_cast<char32_t>(value); }

  template <std::integral _Value> requires(sizeof(_Value) == sizeof(uint8_t )) inline constexpr auto AsUint (_Value value = _Value{ 0 }) noexcept { return static_cast<uint8_t >(value); }
  template <std::integral _Value> requires(sizeof(_Value) == sizeof(uint16_t)) inline constexpr auto AsUint (_Value value = _Value{ 0 }) noexcept { return static_cast<uint16_t>(value); }
  template <std::integral _Value> requires(sizeof(_Value) == sizeof(uint32_t)) inline constexpr auto AsUint (_Value value = _Value{ 0 }) noexcept { return static_cast<uint32_t>(value); }

  enum class CodepointType : int8_t {
    SURROGATE_UPPER = -2,
    SURROGATE_LOWER = -1,
    OUT_OF_RANGE    = +0,
    UTF8_ONE_BYTE   = +1, 
    UTF8_TWO_BYTE   = +2,
    UTF8_THREE_BYTE = +3, 
    UTF8_FOUR_BYTE  = +4
  };

  enum class Encoding: std::uint8_t { 
    UCS1  = 0x01u, 
    ASCII = UCS1, 
    UTF8  = 0x81u, 
    UCS2  = 0x02u, 
    UTF16 = 0x82u, 
    UCS4  = 0x04u, 
    UTF32 = UCS4
  }; 

  template <std::integral _Dest = uint32_t> inline constexpr auto SURROGATE_LOW_START   { static_cast<_Dest>(0x0000DC00u) };
  template <std::integral _Dest = uint32_t> inline constexpr auto SURROGATE_HIGH_START  { static_cast<_Dest>(0x0000D800u) };
  template <std::integral _Dest = uint32_t> inline constexpr auto SURROGATE_MASK        { static_cast<_Dest>(0xFFFFFC00u) };
  template <std::integral _Dest = uint32_t> inline constexpr auto LIMIT4_BYTE           { static_cast<_Dest>(0x00110000u) };
  template <std::integral _Dest = uint32_t> inline constexpr auto LIMIT3_BYTE           { static_cast<_Dest>(0x00010000u) };
  template <std::integral _Dest = uint32_t> inline constexpr auto LIMIT2_BYTE           { static_cast<_Dest>(0x00000800u) };
  template <std::integral _Dest = uint32_t> inline constexpr auto LIMIT1_BYTE           { static_cast<_Dest>(0x00000080u) };
  // Unicode table 3-7: C0 and C1 lead nothing, so two-byte leads start at C2.
  template <std::integral _Dest = uint32_t> inline constexpr auto LEAD_MIN_TWO_BYTE     { static_cast<_Dest>(0x000000C2u) };
  template <std::integral _Dest = char16_t> inline constexpr auto REPLACEMENT_CODEPOINT { static_cast<_Dest>(0x0000FFFDu) };
  template <std::integral _Dest = char32_t> inline constexpr auto INVALID_CODEPOINT     { static_cast<_Dest>(0xFFFFFFFFu) };

  template <std::integral _From>
  inline constexpr auto IsSurrogateUpper(_From value)  noexcept -> bool { 
    return (value & SURROGATE_MASK<>) == SURROGATE_HIGH_START<>; 
  }

  template <std::integral _From>
  inline constexpr auto IsSurrogateLower(_From value)  noexcept -> bool { 
    return (value & SURROGATE_MASK<>) == SURROGATE_LOW_START<>; 
  }

  inline constexpr 
  auto CodepointTriage(std::integral auto input) 
    noexcept -> CodepointType
  {
    using enum CodepointType;
    auto const value{ AsUint(input) };
    switch(value & SURROGATE_MASK<>) 
    {
    case SURROGATE_HIGH_START<>: return SURROGATE_UPPER;
    case SURROGATE_LOW_START<> : return SURROGATE_LOWER;
    default: break;
    }
    if (value < LIMIT1_BYTE<>) return UTF8_ONE_BYTE;
    if (value < LIMIT2_BYTE<>) return UTF8_TWO_BYTE;
    if (value < LIMIT3_BYTE<>) return UTF8_THREE_BYTE;
    if (value < LIMIT4_BYTE<>) return UTF8_FOUR_BYTE; 
    return OUT_OF_RANGE;
  }

  

  inline constexpr auto ValidateCodepoint(
                          CodepointType ctype) 
                        noexcept -> bool 
  { using CPTy = CodepointType;
    switch(ctype) 
    {
    case CPTy::UTF8_ONE_BYTE: 
    case CPTy::UTF8_TWO_BYTE:
    case CPTy::UTF8_THREE_BYTE:
    case CPTy::UTF8_FOUR_BYTE:
      return true;
    case CPTy::SURROGATE_LOWER:
    case CPTy::SURROGATE_UPPER:
    case CPTy::OUT_OF_RANGE:
    default:
      return false;
    }
  }

  inline constexpr 
  auto ValidateCodepoint(std::integral auto value) 
                        noexcept -> bool 
  { return ValidateCodepoint(
      CodepointTriage(value)); }

  template <std::integral _From>
  inline constexpr 
  auto SanitizeCodepoint(_From value) 
    noexcept -> _From 
  { return !ValidateCodepoint(value) 
      ? REPLACEMENT_CODEPOINT<_From>
      : static_cast<_From>(value);
  }
}

namespace oxbox::utilities
{
  using detail::codepoint::CodepointTriage;
  using detail::codepoint::CodepointType;
  using detail::codepoint::Encoding;
  using detail::codepoint::INVALID_CODEPOINT;
  using detail::codepoint::REPLACEMENT_CODEPOINT;
}
