#pragma once
// Code units back up into a code point, one unit at a time; UtfDecodeState
// carries a part-built sequence between units.

#include "oxbox/utilities/codepoint.hpp"

#include <bit>
#include <concepts>
#include <cstdint>
#include <optional>

namespace oxbox::utilities::detail::utf_decode
{
  using std::uint8_t;
  using std::uint16_t;
  using std::uint32_t;

  using codepoint::AsUint;
  using codepoint::IsSurrogateLower;
  using codepoint::IsSurrogateUpper;
  using codepoint::LEAD_MIN_TWO_BYTE;
  using codepoint::LIMIT3_BYTE;
  using codepoint::ValidateCodepoint;

#pragma pack(push, 1)
  struct UtfDecodeState
  {
    uint32_t bits : 28 { 0 };
    uint32_t step :  3 { 0 };
  };
#pragma pack(pop)

  template <std::integral _Dest, std::integral _From>
  inline constexpr auto _UtfDecodeDone(UtfDecodeState& state, _From value) 
    noexcept -> std::optional<_Dest>
  { state = UtfDecodeState{ };
    return static_cast<_Dest>(value);
  }

  template <std::integral _Dest>
  inline constexpr auto _UtfDecodeFail(UtfDecodeState& state) 
    noexcept -> std::optional<_Dest>
  { return _UtfDecodeDone<_Dest>(
      state, INVALID_CODEPOINT<_Dest>);
  }

  template <std::integral _Dest,
            std::unsigned_integral _From>
  requires((sizeof(_Dest) == sizeof(uint32_t)) 
        && (sizeof(_From) == sizeof(uint8_t )))
  inline constexpr auto _UtfDecode(UtfDecodeState& state, _From value)
    noexcept -> std::optional<_Dest>
  { 
    if (state.step == 0u) 
    {
      auto const width{ AsUint(
        std::countl_one(value)) };
      switch (width) 
      {
      case 0u:
        return _UtfDecodeDone<_Dest>(state, 
          static_cast<_Dest>(value & 0x7Fu));
      case 1u: // 10xxxxxx is a trail byte, never a lead (Unicode table 3-7)
      default:
        return _UtfDecodeFail<_Dest>(state);
      case 2u:
        if (value < LEAD_MIN_TWO_BYTE<>) {
          return _UtfDecodeFail<_Dest>(state); }
        [[fallthrough]];
      case 3u:
      case 4u:
        state = UtfDecodeState{ 
          .bits = value & (0x7Fu >> width),
          .step = width };    
        return std::nullopt;
      }
    }
    if (state.step > 6u) { 
      return _UtfDecodeFail<_Dest>(state); }
    if ((value & 0xC0u) != 0x80u) {
      return _UtfDecodeFail<_Dest>(state); } 
    if ((state.step>=3u) && !state.bits) 
    { // Unicode table 3-7: the byte after E0 floors at A0, after F0 at 90
      auto const floor{ 0x80u + (
        0x80u >> (state.step - 1u)) };
      if (value < static_cast<_From>(floor)) {
        return _UtfDecodeFail<_Dest>(state); }
    }
    state = UtfDecodeState{ 
      .bits = (state.bits << 6u) 
            + (value & 0x3Fu),
      .step = (state.step  - 1u) };
    if (state.step < 2u) 
    {
      auto const codepoint{ static_cast<_Dest>(state.bits) };
      if (!ValidateCodepoint(codepoint)) {
        return _UtfDecodeFail<_Dest>(state); }
      return _UtfDecodeDone<_Dest>(state, codepoint);
    }
    return std::nullopt;
  }

  template <std::integral _Dest,
            std::unsigned_integral _From>
  requires((sizeof(_Dest) == sizeof(uint32_t)) 
        && (sizeof(_From) == sizeof(uint16_t)))
  inline constexpr auto _UtfDecode(UtfDecodeState& state, _From value)
    noexcept -> std::optional<_Dest>
  {
    using enum CodepointType; 
    switch(state.step)
    {
    case 0u: 
      if (!IsSurrogateUpper(value)) {
        if (!ValidateCodepoint(value)) break;
        return _UtfDecodeDone<_Dest>(state, value);
      }
      state = UtfDecodeState{ 
        .bits = ((value & 0x3FFu) << 10u) 
              + LIMIT3_BYTE<>,
        .step = 1u };
      return std::nullopt; 
    case 1u:
      if (!IsSurrogateLower(value)) break;
      return _UtfDecodeDone<_Dest>(state, 
              (value & 0x3FFu) + state.bits);
    default: break;
    }     
    return _UtfDecodeFail<_Dest>(state);
  }
    
  template <std::integral _Dest,
            std::unsigned_integral _From>
  requires((sizeof(_Dest) == sizeof(uint32_t)) 
        && (sizeof(_From) == sizeof(uint32_t)))
  inline constexpr auto _UtfDecode(UtfDecodeState& state, _From value)
    noexcept -> std::optional<_Dest>
  { 
    if (ValidateCodepoint(value)) {
      return _UtfDecodeDone<_Dest>(state, value);
    } else {
      return _UtfDecodeFail<_Dest>(state);
    }
  }

  template <std::integral _Dest = char32_t, std::integral _From>
  requires(sizeof(_Dest) == sizeof(uint32_t))
  inline constexpr auto UtfDecode(UtfDecodeState& state, _From input)
    noexcept -> std::optional<_Dest>
  {   
    static_assert(std::has_single_bit(sizeof(_From)) 
              && (sizeof(_From) <= 4u));    
    const auto value{ AsUint(input) };
    return _UtfDecode<_Dest>(state, value);
  }
}

namespace oxbox::utilities
{
  using detail::utf_decode::UtfDecode;
  using detail::utf_decode::UtfDecodeState;
}
