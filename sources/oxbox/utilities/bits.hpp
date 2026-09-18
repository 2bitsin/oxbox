#pragma once
// Alignment, the width-to-type maps and the four bit-field operations.
// Lifted from bossdeux (utilities/integer.hpp, utilities/bitwise.hpp),
// emuex (utilities/integral.hpp) and xoctet (xoctet-bridge/wire.cpp).

#include "oxbox/utilities/short-types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace oxbox::utilities::detail::bits
{
  inline constexpr std::size_t BITS_PER_BYTE{ 8u };
  inline constexpr std::size_t INT_SIZE_MIN  { sizeof(U08) };
  inline constexpr std::size_t INT_SIZE_MAX  { sizeof(U64) };
  inline constexpr std::size_t INT_LENGTH_MIN{ INT_SIZE_MIN * BITS_PER_BYTE };
  inline constexpr std::size_t INT_LENGTH_MAX{ INT_SIZE_MAX * BITS_PER_BYTE };

  template <std::unsigned_integral _T>
  inline constexpr std::size_t BITS_IN{
    static_cast<std::size_t>(std::numeric_limits<_T>::digits) };

  // ---- alignment -------------------------------------------------------

  // A boundary of zero is undefined: x86 traps on the modulo and AArch64's
  // UDIV answers zero, so there is no behaviour to rely on.
  template <std::unsigned_integral _T>
  inline constexpr auto AlignUp(_T value, _T boundary) noexcept -> _T
  {
    if (std::has_single_bit(boundary))
      return static_cast<_T>((value + boundary - _T{ 1 })
                           & static_cast<_T>(~(boundary - _T{ 1 })));
    auto const modulo{ static_cast<_T>(value % boundary) };
    return static_cast<_T>(value + (boundary - modulo) % boundary);
  }

  template <std::size_t _BOUNDARY, std::unsigned_integral _T>
    requires (std::has_single_bit(_BOUNDARY))
  inline constexpr auto AlignUp(_T value) noexcept -> _T
  {
    if constexpr (_BOUNDARY <= 1u)
      return value;
    else
    {
      constexpr auto MASK{ static_cast<_T>(_BOUNDARY - 1u) };
      return static_cast<_T>((value + MASK) & static_cast<_T>(~MASK));
    }
  }

  template <typename _As, std::unsigned_integral _T>
  inline constexpr auto AlignUpAs(_T value) noexcept -> _T
  {
    return AlignUp<alignof(_As), _T>(value);
  }

  template <std::unsigned_integral _T>
  inline constexpr auto IsAligned(_T value, _T boundary) noexcept -> bool
  {
    return value % boundary == _T{ 0 };
  }

  template <std::size_t _BOUNDARY, std::unsigned_integral _T>
    requires (std::has_single_bit(_BOUNDARY))
  inline constexpr auto IsAligned(_T value) noexcept -> bool
  {
    return (value & static_cast<_T>(_BOUNDARY - 1u)) == _T{ 0 };
  }

  // ---- widening --------------------------------------------------------

  template <std::integral _Dst, std::integral _Src>
    requires (sizeof(_Dst) >= sizeof(_Src)
           && std::is_signed_v<_Dst> == std::is_signed_v<_Src>)
  inline constexpr auto IntExtend(_Src value) noexcept -> _Dst
  {
    return static_cast<_Dst>(value);
  }

  // ---- the width-to-type maps -----------------------------------------

  inline constexpr auto IntLengthInRange(std::size_t length) noexcept -> bool
  { return INT_LENGTH_MIN <= length && INT_LENGTH_MAX >= length; }

  inline constexpr auto IntSizeInRange(std::size_t size) noexcept -> bool
  { return IntLengthInRange(size * BITS_PER_BYTE); }

  inline constexpr auto IntLengthValid(std::size_t length) noexcept -> bool
  { return IntLengthInRange(length) && std::has_single_bit(length); }

  inline constexpr auto IntSizeValid(std::size_t size) noexcept -> bool
  { return IntSizeInRange(size) && std::has_single_bit(size); }

  template <std::size_t _SIZE_BYTES>
    requires (IntSizeValid(_SIZE_BYTES))
  struct UIntOfSizeT;

  template <> struct UIntOfSizeT<1u> : std::type_identity<U08> { };
  template <> struct UIntOfSizeT<2u> : std::type_identity<U16> { };
  template <> struct UIntOfSizeT<4u> : std::type_identity<U32> { };
  template <> struct UIntOfSizeT<8u> : std::type_identity<U64> { };

  template <std::size_t _SIZE_BYTES>
    requires (IntSizeValid(_SIZE_BYTES))
  using UIntOfSize = typename UIntOfSizeT<_SIZE_BYTES>::type;

  template <std::size_t _SIZE_BYTES>
    requires (IntSizeValid(_SIZE_BYTES))
  using SIntOfSize = std::make_signed_t<UIntOfSize<_SIZE_BYTES>>;

  template <std::size_t _LENGTH_BITS>
    requires (IntLengthValid(_LENGTH_BITS))
  using UIntOfLength = UIntOfSize<(_LENGTH_BITS / BITS_PER_BYTE)>;

  template <std::size_t _LENGTH_BITS>
    requires (IntLengthValid(_LENGTH_BITS))
  using SIntOfLength = SIntOfSize<(_LENGTH_BITS / BITS_PER_BYTE)>;

  // A field of 5 bits has to live somewhere: the width rounds up to the next
  // real type.
  template <std::size_t _SIZE_BYTES>
    requires (_SIZE_BYTES <= INT_SIZE_MAX)
  using UIntOfAtLeastSize = UIntOfSize<std::bit_ceil(std::max<std::size_t>(_SIZE_BYTES, 1u))>;

  template <std::size_t _SIZE_BYTES>
    requires (_SIZE_BYTES <= INT_SIZE_MAX)
  using SIntOfAtLeastSize = SIntOfSize<std::bit_ceil(std::max<std::size_t>(_SIZE_BYTES, 1u))>;

  template <std::size_t _LENGTH_BITS>
    requires (_LENGTH_BITS <= INT_LENGTH_MAX)
  using UIntOfAtLeastLength =
    UIntOfAtLeastSize<((_LENGTH_BITS + BITS_PER_BYTE - 1u) / BITS_PER_BYTE)>;

  template <std::size_t _LENGTH_BITS>
    requires (_LENGTH_BITS <= INT_LENGTH_MAX)
  using SIntOfAtLeastLength =
    SIntOfAtLeastSize<((_LENGTH_BITS + BITS_PER_BYTE - 1u) / BITS_PER_BYTE)>;

  // ---- bit fields ------------------------------------------------------

  // Answers at `length == BITS_IN<_T>` too, where `(1 << length) - 1` would
  // be undefined.
  template <std::unsigned_integral _T>
  inline constexpr auto LowBits(std::size_t length) noexcept -> _T
  {
    if (length >= BITS_IN<_T>)
      return static_cast<_T>(~_T{ 0 });
    return static_cast<_T>((_T{ 1 } << length) - _T{ 1 });
  }

  // A field running off the top is truncated to what is there and an offset
  // past the top is empty, so nothing ever shifts by more than the width.
  template <std::unsigned_integral _T>
  inline constexpr auto ExtractBits(_T value, std::size_t offset,
                                    std::size_t length) noexcept -> _T
  {
    if (offset >= BITS_IN<_T>)
      return _T{ 0 };
    auto const fits{ std::min(length, BITS_IN<_T> - offset) };
    return static_cast<_T>((value >> offset) & LowBits<_T>(fits));
  }

  // `field` is masked to the field's width rather than spilling into its
  // neighbours.
  template <std::unsigned_integral _T>
  inline constexpr auto ReplaceBits(_T value, std::size_t offset,
                                    std::size_t length, _T field) noexcept -> _T
  {
    if (offset >= BITS_IN<_T>)
      return value;
    auto const fits{ std::min(length, BITS_IN<_T> - offset) };
    auto const mask{ static_cast<_T>(LowBits<_T>(fits) << offset) };
    return static_cast<_T>((value & static_cast<_T>(~mask))
                         | (static_cast<_T>(field << offset) & mask));
  }

  // Most significant field first, anchored at the low end: the widths are
  // summed and the first field starts at that sum, so `ExplodeBits<4>(0xAB)`
  // is {0xB}. `ExplodeBits<2, 3, 3>(0xC3)` is mod, reg, rm.
  template <std::size_t... _BITS>
  struct ExplodeBitsFn
  {
    template <std::unsigned_integral _T>
    constexpr auto operator()(_T value) const noexcept
      -> std::array<_T, sizeof...(_BITS)>
    {
      static_assert((_BITS + ... + 0u) <= BITS_IN<_T>,
                    "ExplodeBits: the fields are wider than the value");
      std::array<_T, sizeof...(_BITS)> out{ };
      std::size_t shift{ (_BITS + ... + 0u) };
      std::size_t index{ 0u };
      ((shift -= _BITS,
        out[index++] = ExtractBits(value, shift, _BITS)), ...);
      return out;
    }
  };

  template <std::size_t... _BITS>
  struct CompactBitsFn
  {
    template <std::unsigned_integral... _T>
      requires (sizeof...(_BITS) == sizeof...(_T) && sizeof...(_BITS) > 0u)
    constexpr auto operator()(_T... values) const noexcept
      -> UIntOfAtLeastLength<(_BITS + ...)>
    {
      static_assert((_BITS + ...) <= INT_LENGTH_MAX,
                    "CompactBits: the fields are wider than 64 bits");
      using _Result = UIntOfAtLeastLength<(_BITS + ...)>;
      _Result out{ };
      std::size_t shift{ (_BITS + ...) };
      ((shift -= _BITS,
        out = static_cast<_Result>(
          out | static_cast<_Result>(
            static_cast<_Result>(values & LowBits<_T>(_BITS)) << shift))), ...);
      return out;
    }
  };

  // NOLINTBEGIN(readability-identifier-naming): PascalCase functor instances
  template <std::size_t... _BITS>
  inline constexpr ExplodeBitsFn<_BITS...> ExplodeBits{ };

  template <std::size_t... _BITS>
  inline constexpr CompactBitsFn<_BITS...> CompactBits{ };
  // NOLINTEND(readability-identifier-naming)
}

namespace oxbox::utilities
{
  using detail::bits::AlignUp;
  using detail::bits::AlignUpAs;
  using detail::bits::BITS_IN;
  using detail::bits::BITS_PER_BYTE;
  using detail::bits::CompactBits;
  using detail::bits::ExplodeBits;
  using detail::bits::ExtractBits;
  using detail::bits::INT_LENGTH_MAX;
  using detail::bits::INT_SIZE_MAX;
  using detail::bits::IntExtend;
  using detail::bits::IntLengthValid;
  using detail::bits::IntSizeValid;
  using detail::bits::IsAligned;
  using detail::bits::LowBits;
  using detail::bits::ReplaceBits;
  using detail::bits::SIntOfAtLeastLength;
  using detail::bits::SIntOfAtLeastSize;
  using detail::bits::SIntOfLength;
  using detail::bits::SIntOfSize;
  using detail::bits::UIntOfAtLeastLength;
  using detail::bits::UIntOfAtLeastSize;
  using detail::bits::UIntOfLength;
  using detail::bits::UIntOfSize;
}
