#pragma once
// A scalar in and out of a run of bytes at a named byte order, and the two
// cursors that do it against a bound. Fetch/Store throw on a short span;
// the cursors latch one sticky flag instead. Bounded pair from xoctet's
// sources/xoctet-bridge/wire.cpp.

#include "oxbox/utilities/bits.hpp"
#include "oxbox/utilities/short-types.hpp"
#include "oxbox/utilities/span.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace oxbox::utilities::detail::serialization {

  struct NoAdvanceFlagT { };
  inline constexpr NoAdvanceFlagT NoAdvance{ };

  // Floating point rides the same path because the swap goes through the
  // same-width unsigned type, which make_unsigned has no answer for.
  template <typename _Type>
  concept Scalar = std::integral<_Type> || std::floating_point<_Type>;

  template <Scalar _Type>
  using SwapAs = UIntOfSize<sizeof(_Type)>;

  template <Scalar _Type,
            std::endian ENDIAN = std::endian::native,
            typename _Src>
    requires std::is_trivially_copyable_v<_Src>
  inline constexpr auto Fetch(std::span<_Src const> source, NoAdvanceFlagT) -> _Type {
    auto const bytes{ AsBytes(source) };
    if (bytes.size() < sizeof(_Type)) { throw std::out_of_range{
      "Fetch: input span shorter than sizeof(T)" }; }
    std::array<std::byte, sizeof(_Type)> buf{ };
    std::copy_n(bytes.begin(), sizeof(_Type), buf.begin());
    // An else and not a fall-through: cl reports the other line as dead code
    // at /W4 (C4702) on every non-native-endian instantiation.
    if constexpr (ENDIAN != std::endian::native) {
      using Unsigned = SwapAs<_Type>;
      return std::bit_cast<_Type>(std::byteswap(std::bit_cast<Unsigned>(buf))); }
    else {
      return std::bit_cast<_Type>(buf); }
  }

  template <Scalar _Type, std::endian ENDIAN = std::endian::native>
  inline constexpr auto Fetch(Bytes& bytes) -> _Type {
    auto const value{ Fetch<_Type, ENDIAN>(bytes, NoAdvance) };
    Advance(bytes, sizeof(_Type));
    return value;
  }

  template <Scalar _Type, std::endian ENDIAN = std::endian::native, typename STy, std::size_t EXTENT>
  requires(std::is_trivially_copyable_v<STy>)
  inline constexpr auto Fetch(std::span<STy const, EXTENT> src, NoAdvanceFlagT) -> _Type {
    return Fetch<_Type, ENDIAN>(AsBytes(src), NoAdvance);
  }

  template <Scalar _Type, std::endian ENDIAN = std::endian::native,
            typename _Dst, std::size_t EXTENT>
    requires std::is_trivially_copyable_v<_Dst>
  inline constexpr auto Store(_Type value, std::span<_Dst, EXTENT> dest, NoAdvanceFlagT) -> void {
    auto const bytes{ AsWritableBytes(dest) };
    if (bytes.size() < sizeof(_Type)) { throw std::out_of_range{
      "Store: output span shorter than sizeof(T)" }; }
    if constexpr (ENDIAN != std::endian::native) {
      using Unsigned = SwapAs<_Type>;
      value = std::bit_cast<_Type>(std::byteswap(std::bit_cast<Unsigned>(value))); }
    std::ranges::copy(std::bit_cast<std::array<std::byte, sizeof(_Type)>>(value),
                      bytes.begin());
  }

  template <Scalar _Type, std::endian ENDIAN = std::endian::native>
  inline constexpr auto Store(_Type value, WritableBytes& dest) -> void {
    Store<_Type, ENDIAN>(value, dest, NoAdvance);
    Advance(dest, sizeof(_Type));
  }


  template <Scalar _Type, typename...Args>
  requires ((sizeof...(Args) >= 1u) && (sizeof...(Args) <= 2u))
  inline constexpr auto Fetch(std::endian order, Args&&...args) -> _Type {
    switch(order)
    {
    case std::endian::little:
      return Fetch<_Type, std::endian::little>(std::forward<Args>(args)...);
    case std::endian::big:
      return Fetch<_Type, std::endian::big>(std::forward<Args>(args)...);
    default:
      return Fetch<_Type, std::endian::native>(std::forward<Args>(args)...);
    }               
  }

  template <Scalar _Type, typename...Args>
  requires ((sizeof...(Args) >= 2u) && (sizeof...(Args) <= 3u))
  inline constexpr auto Store(std::endian order, Args&&...args) -> void {
    switch(order) 
    {
    case std::endian::little: 
      return Store<_Type, std::endian::little>(std::forward<Args>(args)...);
    case std::endian::big: 
      return Store<_Type, std::endian::big>(std::forward<Args>(args)...);
    default:      
      return Store<_Type, std::endian::native>(std::forward<Args>(args)...);
    }
  }


  // Every step past the end answers with a default and latches the flag, so a
  // layout reads as a list of fields and Sound() is asked once at the bottom.
  // A reader that has gone unsound never becomes sound again.
  class BoundedReader
  {
  public:
    explicit constexpr BoundedReader(Bytes from) noexcept : _from{ from } { }

    // The comparison is against what is left: adding the count to the cursor
    // is the shape that wraps.
    constexpr auto Take(std::size_t count) noexcept -> Bytes
    {
      if (!_sound || count > _from.size() - _at)
      {
        _sound = false;
        return { };
      }
      auto const taken{ _from.subspan(_at, count) };
      _at += count;
      return taken;
    }

    // The free Fetch's throw is unreachable here: Take answers with exactly
    // sizeof(_Type) bytes or with nothing.
    template <Scalar _Type, std::endian ENDIAN = std::endian::native>
    constexpr auto Fetch() noexcept -> _Type
    {
      auto const taken{ Take(sizeof(_Type)) };
      if (taken.empty())
        return _Type{ };
      return detail::serialization::Fetch<_Type, ENDIAN>(taken, NoAdvance);
    }

    // A view into the source and not a copy, so the caller's bytes have to
    // outlive it. Not constexpr: the cast from bytes to chars is a
    // reinterpretation, which no constant evaluation may perform.
    auto Text(std::size_t count) noexcept -> std::string_view
    {
      auto const taken{ Take(count) };
      if (taken.empty())
        return { };
      return std::string_view{ reinterpret_cast<char const*>(taken.data()),
                               taken.size() };
    }

    // Counted from the start of the source, which is what the writer pads to.
    constexpr auto Align(std::size_t boundary) noexcept -> void
    {
      static_cast<void>(Take(AlignUp(_at, boundary) - _at));
    }

    // For the layout's own judgement -- a magic number this build did not
    // write, a version it cannot interpret -- so the caller still asks one
    // question at the bottom. One direction only: nothing makes a reader sound.
    constexpr auto Refuse() noexcept -> void { _sound = false; }

    [[nodiscard]] constexpr auto Sound() const noexcept -> bool { return _sound; }
    [[nodiscard]] constexpr auto At() const noexcept -> std::size_t { return _at; }
    [[nodiscard]] constexpr auto Remaining() const noexcept -> std::size_t
    { return _sound ? _from.size() - _at : 0u; }

  private:
    Bytes       _from;
    std::size_t _at{ 0u };
    bool        _sound{ true };
  };

  // The mirror image. A step that will not fit writes nothing -- never a
  // partial scalar -- and latches.
  class BoundedWriter
  {
  public:
    explicit constexpr BoundedWriter(WritableBytes into) noexcept : _into{ into } { }

    constexpr auto Claim(std::size_t count) noexcept -> WritableBytes
    {
      if (!_sound || count > _into.size() - _at)
      {
        _sound = false;
        return { };
      }
      auto const claimed{ _into.subspan(_at, count) };
      _at += count;
      return claimed;
    }

    template <Scalar _Type, std::endian ENDIAN = std::endian::native>
    constexpr auto Store(_Type value) noexcept -> void
    {
      auto const claimed{ Claim(sizeof(_Type)) };
      if (claimed.empty())
        return;
      detail::serialization::Store<_Type, ENDIAN>(value, claimed, NoAdvance);
    }

    constexpr auto Put(Bytes value) noexcept -> void
    {
      auto const claimed{ Claim(value.size()) };
      if (claimed.size() != value.size())
        return;
      std::ranges::copy(value, claimed.begin());
    }

    constexpr auto Text(std::string_view value) noexcept -> void
    {
      auto const claimed{ Claim(value.size()) };
      if (claimed.size() != value.size())
        return;
      std::ranges::transform(value, claimed.begin(), [](char letter) {
        return std::byte{ static_cast<U08>(letter) }; });
    }

    // The padding is written and not skipped: a gap whose content depends on
    // the allocator is a frame that cannot be compared or hashed.
    constexpr auto Align(std::size_t boundary) noexcept -> void
    {
      std::ranges::fill(Claim(AlignUp(_at, boundary) - _at), std::byte{ 0u });
    }

    // The mirror of the reader's Refuse, for a caller that has discovered the
    // frame cannot be built at all.
    constexpr auto Refuse() noexcept -> void { _sound = false; }

    [[nodiscard]] constexpr auto Sound() const noexcept -> bool { return _sound; }
    [[nodiscard]] constexpr auto At() const noexcept -> std::size_t { return _at; }
    [[nodiscard]] constexpr auto Remaining() const noexcept -> std::size_t
    { return _sound ? _into.size() - _at : 0u; }

    // Sized in advance and now exactly full: what a measure-then-write pass
    // checks instead of trusting that its two passes agreed.
    [[nodiscard]] constexpr auto Whole() const noexcept -> bool
    { return _sound && _at == _into.size(); }

  private:
    WritableBytes _into;
    std::size_t   _at{ 0u };
    bool          _sound{ true };
  };

}

namespace oxbox::utilities {
  using detail::serialization::BoundedReader;
  using detail::serialization::BoundedWriter;
  using detail::serialization::Fetch;
  using detail::serialization::NoAdvance;
  using detail::serialization::Scalar;
  using detail::serialization::Store;
}
