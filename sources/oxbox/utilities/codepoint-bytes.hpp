#pragma once
// One code point across the byte boundary, at a named encoding and byte
// order; a short buffer is answered by how much it was short.

#include "oxbox/utilities/codepoint.hpp"
#include "oxbox/utilities/serdes.hpp"
#include "oxbox/utilities/span.hpp"
#include "oxbox/utilities/utf-decode.hpp"
#include "oxbox/utilities/utf-encode.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>

namespace oxbox::utilities::detail::codepoint_bytes
{
  namespace stdr = std::ranges;
  namespace stdv = stdr::views;

  using std::intptr_t;
  using std::uint8_t;
  using std::uint16_t;
  using std::uint32_t;

  using codepoint::AsUint;

  using utilities::WritableBytes;
  using utilities::Bytes;
  using utilities::Fetch;
  using utilities::Store;
  using ConstantBytes = Bytes;

  template <std::integral _Dest, std::integral _From> 
  inline constexpr 
  auto _UtfDecodeFromBytes(ConstantBytes& bytes, std::endian order)
                           noexcept -> std::optional<_Dest>
  {
    using std::optional;
    UtfDecodeState state{ };
    optional<_Dest> code{ };
    auto buff{ bytes   };
    while ((sizeof(_From) <= buff.size_bytes())
           && !code.has_value())
    { code = UtfDecode<_Dest>(state, 
        Fetch<_From>(order, buff));
    }
    constexpr auto FAIL{ 
      INVALID_CODEPOINT<_Dest> };
    if(FAIL != code.value_or(FAIL)) {
      bytes = buff;
      return code.value(); }
    return std::nullopt;
  }
  template <std::integral _Dest, std::integral _From> 
  inline constexpr 
  auto _UcsDecodeFromBytes(ConstantBytes& bytes, 
                           std::endian    order)
                           noexcept -> std::optional<_Dest>
  {
    if (bytes.size_bytes() < sizeof(_From)) return std::nullopt;
    return{ static_cast<_Dest>(Fetch<_From>(order, bytes)) }; 
  }

  template <std::integral _Dest = char32_t> 
  requires (sizeof(_Dest) == sizeof(uint32_t))
  inline constexpr 
  auto DecodeFromBytes(ConstantBytes& bytes,
                       Encoding       encoding = Encoding::UTF8, 
                       std::endian    order    = std::endian::native)
                       noexcept -> std::optional<_Dest>
  {
    switch(encoding)
    {
    using enum Encoding;
    case UCS1  : return _UcsDecodeFromBytes<_Dest, uint8_t >(bytes, order);
    case UCS2  : return _UcsDecodeFromBytes<_Dest, uint16_t>(bytes, order);
    case UCS4  : return _UcsDecodeFromBytes<_Dest, uint32_t>(bytes, order);
    case UTF8  : return _UtfDecodeFromBytes<_Dest, uint8_t >(bytes, order);
    case UTF16 : return _UtfDecodeFromBytes<_Dest, uint16_t>(bytes, order);
    }
    return std::nullopt;
  }

  template <std::integral _Dest, std::integral _From> 
  inline constexpr 
  auto _UcsEncodeIntoBytes(_From          value, 
                           WritableBytes& bytes, 
                           std::endian    order)
                           noexcept ->    std::intptr_t
  { constexpr auto SIZE{ sizeof(_Dest) };
    auto const outcp{ static_cast<_Dest>(value) };
    if (AsUint(outcp) != AsUint(value)) { return 0u; }
    if (bytes.size_bytes() < SIZE)
      return static_cast<intptr_t>(bytes.size_bytes()) - SIZE;
    Store<_Dest>(order, outcp, bytes);
    return SIZE;
  }

  template <std::integral _Dest, std::integral _From> 
  inline constexpr 
  auto _UtfEncodeIntoBytes(_From          value, 
                           WritableBytes& bytes, 
                           std::endian    order)
                           noexcept ->    std::intptr_t
  { constexpr auto SIZE{ sizeof(_Dest) };
    auto place{ bytes };
    auto const [length, sequence]{ 
      UtfEncode<_Dest>(value) };          
    if (length < 1u) { return 0u; }
    auto const needed{ length * SIZE };
    if (needed > place.size_bytes())
      return static_cast<intptr_t>(place.size_bytes()) -
             static_cast<intptr_t>(needed);
    for(auto&& value: sequence
                    | stdv::take(length))
    {
      Store<_Dest>(order, value, place);
    }
    auto const total{ 
      static_cast<intptr_t>(bytes.size_bytes()) - 
      static_cast<intptr_t>(place.size_bytes()) };
    bytes = place;
    return total; 
  }
  template <std::integral _From> 
  requires (sizeof(_From) == sizeof(uint32_t))
  constexpr inline auto EncodeIntoBytes(_From          value,
                                        WritableBytes& bytes,
                                        Encoding       encoding = Encoding::UTF8, 
                                        std::endian    order = std::endian::native)
                                        noexcept ->    std::intptr_t
  {
    switch(encoding)
    {
    using enum Encoding;
    case UCS1:  return _UcsEncodeIntoBytes<uint8_t >(value, bytes, order);
    case UCS2:  return _UcsEncodeIntoBytes<uint16_t>(value, bytes, order);
    case UCS4:  return _UcsEncodeIntoBytes<uint32_t>(value, bytes, order);
    case UTF8:  return _UtfEncodeIntoBytes<uint8_t >(value, bytes, order);
    case UTF16: return _UtfEncodeIntoBytes<uint16_t>(value, bytes, order); 
    }
    return 0u;
  }
}

namespace oxbox::utilities
{
  using detail::codepoint_bytes::DecodeFromBytes;
  using detail::codepoint_bytes::EncodeIntoBytes;
}
