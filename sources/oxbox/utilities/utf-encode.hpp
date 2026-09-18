#pragma once
// A code point down into the code units of a named width: 8, 16 or 32 bits,
// answered as a length and a fixed array.

#include "oxbox/utilities/codepoint.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <tuple>
#include <utility>

namespace oxbox::utilities::detail::utf_encode
{
  using std::uint8_t;

  using codepoint::AsUint;
  using codepoint::LIMIT3_BYTE;
  using codepoint::SURROGATE_HIGH_START;
  using codepoint::SURROGATE_LOW_START;
  using codepoint::ValidateCodepoint;

  template <std::integral _Dest0, std::integral... _DestN>
  requires ((std::same_as<_Dest0, _DestN> && ...) && (
    (4u / sizeof(_Dest0)) > sizeof...(_DestN)))
  inline constexpr auto _Encoded(_Dest0 cp0, _DestN...cpN) noexcept 
    -> std::tuple<uint8_t, std::array<_Dest0, (4u / sizeof(_Dest0))>>
  { return{ 1u + sizeof...(_DestN), { cp0, cpN... } }; }

  template <std::integral _Dest>
  inline constexpr auto _Encoded() noexcept 
    -> decltype(_Encoded(_Dest{})) 
  { return { 0u, { 0u } }; }

  template <std::integral _Dest, std::unsigned_integral _From>
  requires(sizeof(_Dest) == sizeof(uint32_t))
  inline constexpr auto _UtfEncode(_From value, 
                          CodepointType ctype) 
    noexcept -> decltype(_Encoded<_Dest>()) 
  {    
    auto const outcp{ 
      static_cast<_Dest>(value) };
    if (ValidateCodepoint(ctype)) { 
      return _Encoded(outcp); }
    std::unreachable();
    return _Encoded<_Dest>();
  }

  template <std::integral _Dest, std::unsigned_integral _From>
  requires(sizeof(_Dest) == sizeof(uint16_t))
  inline constexpr auto _UtfEncode(_From value, CodepointType ctype) 
    noexcept -> decltype(_Encoded<_Dest>()) 
  {    
    constexpr auto HI{ SURROGATE_HIGH_START<_Dest> };
    constexpr auto LO{ SURROGATE_LOW_START<_Dest> };
    using CPTy = CodepointType;
    switch (ctype) 
    {        
    case CPTy::UTF8_ONE_BYTE:
    case CPTy::UTF8_TWO_BYTE:
    case CPTy::UTF8_THREE_BYTE:
      return _Encoded(static_cast<_Dest>(value));
    case CPTy::UTF8_FOUR_BYTE:
      value -= LIMIT3_BYTE<>;
      return _Encoded(
        static_cast<_Dest>(HI + ((value >> 10u) & 0x3FFu)),
        static_cast<_Dest>(LO + ((value >> 00u) & 0x3FFu)));
    default:
      std::unreachable();
      return _Encoded<_Dest>();
    }
  }

  template <std::integral _Dest, std::unsigned_integral _From>
  requires(sizeof(_Dest) == sizeof(uint8_t))
  inline constexpr auto _UtfEncode(_From value, CodepointType ctype) 
    noexcept -> decltype(_Encoded<_Dest>()) 
  {    
    using CPTy = CodepointType;
    switch (ctype) 
    {
    case CPTy::UTF8_ONE_BYTE:
      return _Encoded(
        static_cast<_Dest>(0x00u + ((value >> 0x00u) & 0x7Fu)));
    case CPTy::UTF8_TWO_BYTE:
      return _Encoded(
        static_cast<_Dest>(0xC0u + ((value >> 0x06u) & 0x1Fu)),
        static_cast<_Dest>(0x80u + ((value >> 0x00u) & 0x3Fu)));        
    case CPTy::UTF8_THREE_BYTE:
      return _Encoded(
        static_cast<_Dest>(0xE0u + ((value >> 0x0Cu) & 0x0Fu)),
        static_cast<_Dest>(0x80u + ((value >> 0x06u) & 0x3Fu)),
        static_cast<_Dest>(0x80u + ((value >> 0x00u) & 0x3Fu)));
    case CPTy::UTF8_FOUR_BYTE:
      return _Encoded(
        static_cast<_Dest>(0xF0u + ((value >> 0x12u) & 0x07u)),
        static_cast<_Dest>(0x80u + ((value >> 0x0Cu) & 0x3Fu)),
        static_cast<_Dest>(0x80u + ((value >> 0x06u) & 0x3Fu)),
        static_cast<_Dest>(0x80u + ((value >> 0x00u) & 0x3Fu)));        
    default:
      std::unreachable();
      return _Encoded<_Dest>();
    }    
  }

  template <std::integral _Dest, std::integral _From>
  inline constexpr auto UtfEncode(_From input) noexcept 
    -> decltype(_Encoded<_Dest>())
  {
    static_assert((sizeof(_Dest) <= 4u) && 
      std::has_single_bit(sizeof(_Dest)));
    auto value{ AsUint(input) };
    auto ctype{ CodepointTriage(value) };
    if (!ValidateCodepoint(ctype)) {
      value = REPLACEMENT_CODEPOINT<>;
      ctype = CodepointTriage(value); }
    return _UtfEncode<_Dest>(value, ctype);      
  }
}

namespace oxbox::utilities
{
  using detail::utf_encode::UtfEncode;
}
