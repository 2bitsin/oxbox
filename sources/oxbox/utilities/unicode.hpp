#pragma once
// The public entry point of the code point surface: the headers below, plus
// the one crossing that needs both directions at once.

#include "oxbox/utilities/buffer-decode-iterator.hpp"
#include "oxbox/utilities/buffer-encode-iterator.hpp"
#include "oxbox/utilities/codepoint-bytes.hpp"
#include "oxbox/utilities/codepoint.hpp"
#include "oxbox/utilities/utf-decode.hpp"
#include "oxbox/utilities/utf-encode.hpp"

#include <concepts>

namespace oxbox::utilities::detail::unicode
{
  using utf_encode::_Encoded;

  template <std::integral _Dest, std::integral _From>
  constexpr inline auto UtfTranscode(
    UtfDecodeState& state, _From input) 
    noexcept -> decltype(_Encoded<_Dest>())
  { 
    auto const outcp{ 
      UtfDecode<char32_t>(state, input) };
    if (!outcp) { return _Encoded<_Dest>(); }
    return UtfEncode<_Dest>(outcp.value());    
  }
}

namespace oxbox::utilities
{
  using detail::unicode::UtfTranscode;
}
