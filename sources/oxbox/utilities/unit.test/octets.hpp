#pragma once
// The octets a test hands a decoder, as bytes, with no cast noise.

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace oxbox::utilities::test
{
  template <std::integral... _Octets>
  constexpr auto Octets(_Octets... octets) noexcept
    -> std::array<std::byte, sizeof...(_Octets)>
  { return { std::byte{ static_cast<std::uint8_t>(octets) }... }; }
}
