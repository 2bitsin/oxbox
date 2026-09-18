#pragma once

#include "oxbox/utilities/short-types.hpp"

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>

namespace oxbox::utilities::detail::span
{
  // subspan from `offset`, clamped: an offset at or past the end yields an empty span
  template <typename _T, std::size_t N>
  inline constexpr auto SafeSubspan(std::span<_T, N> what, std::size_t offset)
    -> std::span<_T>
  {
    return offset < what.size() ? what.subspan(offset) : std::span<_T>{};
  }

  // advance the span past `delta` elements; at or past the end clamps to empty
  template <typename _T, std::size_t N>
  inline constexpr auto Advance(std::span<_T, N>& what, std::size_t delta) -> void
  {
    what = SafeSubspan(what, delta);
  }

  template <typename DTy, typename STy>
    requires std::is_trivially_copyable_v<std::remove_const_t<STy>>
          && std::is_trivially_copyable_v<std::remove_const_t<DTy>>
  inline constexpr auto SpanCast(std::span<STy> source) -> std::span<DTy> {
    std::size_t const out_size{ source.size() * sizeof(STy) / sizeof(DTy) };
    return std::span<DTy>{
      std::bit_cast<DTy*>(source.data()), out_size };
  }


  template <typename T>
  concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

// 
  template <std::ranges::contiguous_range R>
    requires TriviallyCopyable<std::ranges::range_value_t<R>>
  inline constexpr auto AsBytes(R const& range) -> Bytes {
    if constexpr (std::same_as<std::ranges::range_value_t<R>, std::byte>)
      return std::span{ range };                  // already bytes: constant-evaluable
    else
      return std::as_bytes(std::span{ range });   // reinterpreting view: runtime only
  }

  // A single trivially-copyable object (scalar / POD).
  template <TriviallyCopyable T>
    requires (!std::ranges::contiguous_range<T>)
  inline constexpr auto AsBytes(T const& object) -> Bytes {
    return std::as_bytes(std::span<T const, 1>{ std::addressof(object), 1u });
  }

  template <std::ranges::contiguous_range R>
    requires TriviallyCopyable<std::ranges::range_value_t<R>>
  inline constexpr auto AsWritableBytes(R&& range) -> WritableBytes {
    if constexpr (std::same_as<std::ranges::range_value_t<R>, std::byte>)
      return std::span{ range };                  // already bytes: constant-evaluable
    else
      return std::as_writable_bytes(std::span{ range });
  }

  template <TriviallyCopyable T>
    requires (!std::ranges::contiguous_range<T>)
  inline constexpr auto AsWritableBytes(T& object) -> WritableBytes {
    return std::as_writable_bytes(std::span<T, 1>{ std::addressof(object), 1u });
  }

  template <std::integral G=U64, typename Type>
  requires (std::is_trivially_copyable_v<Type>)
  inline constexpr auto BytesEqual(Type const& lhs, Type const& rhs) -> bool {
    namespace str = std::ranges;    
    namespace srv = std::ranges::views;
    auto const b_lhs{ AsBytes(lhs) };
    auto const g_lhs{ SpanCast<G const>(AsBytes(lhs)) };
    auto const b_rhs{ AsBytes(rhs) };
    auto const g_rhs{ SpanCast<G const>(AsBytes(rhs)) };
    if (!str::equal(g_lhs, g_rhs)) { return false; }
    if constexpr (sizeof(Type) % sizeof(G)) {
      auto const drop{ srv::drop(g_lhs.size_bytes()) };
      return str::equal(b_lhs|drop, b_rhs|drop); }
    return true;
  }

}

namespace oxbox::utilities
{
  using detail::span::Advance;
  using detail::span::AsBytes;
  using detail::span::AsWritableBytes;
  using detail::span::BytesEqual;
  using detail::span::SafeSubspan;
  using detail::span::SpanCast;
}