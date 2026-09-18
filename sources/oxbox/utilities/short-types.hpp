#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace oxbox::utilities
{
  
  using U08 = std::uint8_t;
  using U16 = std::uint16_t;
  using U32 = std::uint32_t;
  using U64 = std::uint64_t;
  using S08 = std::int8_t;
  using S16 = std::int16_t;
  using S32 = std::int32_t;
  using S64 = std::int64_t;
  using F32 = float;                    // IEEE-754 assumed, like the U types
  using F64 = double;                   //   assume exact widths

  using Bytes         = std::span<std::byte const>;
  using WritableBytes = std::span<std::byte>;

  inline constexpr auto U08MAX = std::numeric_limits<U08>::max();
  inline constexpr auto U16MAX = std::numeric_limits<U16>::max();
  inline constexpr auto U32MAX = std::numeric_limits<U32>::max();
  inline constexpr auto U64MAX = std::numeric_limits<U64>::max();  

}
