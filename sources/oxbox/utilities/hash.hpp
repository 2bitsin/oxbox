#pragma once
// FNV-1a at 32 and 64 bits, and the literal that makes a string a case
// label. Lifted from qwenlab's sources/utilities/hash.hpp and xoctet's
// sources/xoctet-bridge/self-test-fixture.hpp.

#include "oxbox/utilities/short-types.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <string_view>

namespace oxbox::utilities::detail::hash
{
  // The parameters and unit.test/hash.cpp's vectors are draft-eastlake-fnv's.
  template <std::unsigned_integral _Hash>
  struct Fnv1aParameters;

  template <>
  struct Fnv1aParameters<U32>
  {
    static constexpr U32 OFFSET_BASIS{ 2166136261u };   // 0x811c9dc5
    static constexpr U32 PRIME       { 16777619u   };   // 0x01000193
  };

  template <>
  struct Fnv1aParameters<U64>
  {
    static constexpr U64 OFFSET_BASIS{ 14695981039346656037u };  // 0xcbf29ce484222325
    static constexpr U64 PRIME       { 1099511628211u        };  // 0x00000100000001b3
  };

  template <typename _Hash>
  concept Fnv1aWidth = std::unsigned_integral<_Hash>
                    && requires { Fnv1aParameters<_Hash>::PRIME; };

  template <Fnv1aWidth _Hash = U64>
  inline constexpr _Hash FNV1A_BASIS{ Fnv1aParameters<_Hash>::OFFSET_BASIS };

  // XOR the octet in, then multiply: that ordering is FNV-1a and not FNV-1.
  template <Fnv1aWidth _Hash>
  inline constexpr auto Fnv1aStep(_Hash running, U08 octet) noexcept -> _Hash
  {
    return static_cast<_Hash>(
      static_cast<_Hash>(running ^ octet) * Fnv1aParameters<_Hash>::PRIME);
  }

  template <Fnv1aWidth _Hash = U64>
  inline constexpr auto Fnv1a(Bytes bytes,
                              _Hash running = FNV1A_BASIS<_Hash>) noexcept -> _Hash
  {
    auto const mix = [](_Hash carried, std::byte octet) noexcept {
      return Fnv1aStep<_Hash>(carried, std::to_integer<U08>(octet));
    };
    return std::ranges::fold_left(bytes, running, mix);
  }

  // Bytes and not characters: encoding-agnostic and case-sensitive. Not
  // Fnv1a(AsBytes(text)), because a reinterpreting view is not
  // constant-evaluable and a case label has to fold.
  template <Fnv1aWidth _Hash = U64>
  inline constexpr auto Fnv1a(std::string_view text,
                              _Hash running = FNV1A_BASIS<_Hash>) noexcept -> _Hash
  {
    // char may be signed, and a sign-extended high byte would hash differently.
    auto const mix = [](_Hash carried, char letter) noexcept {
      return Fnv1aStep<_Hash>(carried, static_cast<U08>(letter));
    };
    return std::ranges::fold_left(text, running, mix);
  }

  inline constexpr auto HashString(std::string_view text) noexcept -> U64
  {
    return Fnv1a<U64>(text);
  }

  inline namespace literals
  {
    // The signature is the language's; the raw char const* is not a choice.
    inline constexpr auto operator""_hash(char const* text,
                                          std::size_t length) noexcept -> U64
    {
      return HashString(std::string_view{ text, length });
    }
  }
}

namespace oxbox::utilities
{
  using detail::hash::FNV1A_BASIS;
  using detail::hash::Fnv1a;
  using detail::hash::Fnv1aParameters;
  using detail::hash::Fnv1aWidth;
  using detail::hash::HashString;

  inline namespace literals
  {
    using detail::hash::literals::operator""_hash;
  }
}
