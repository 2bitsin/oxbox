#pragma once

#include <cstddef>
#include <string_view>
#include <algorithm>
#include <ranges>

namespace oxbox::utilities::detail::fixed_string
{

  template <typename _CharT, std::size_t _CAPACITY>
  struct FixedString
  {
    using CharType = _CharT;

    using StringView = std::basic_string_view<CharType>;

    inline static constexpr auto CAPACITY{ _CAPACITY };
    inline static constexpr auto size() -> std::size_t { return CAPACITY; }

    CharType data [CAPACITY];   
    // NOLINT(misc-non-private-member-variables-in-classes): a structural NTTP string

    consteval FixedString(CharType const (&_data)[_CAPACITY]) {
      namespace stdr = std::ranges;
      stdr::copy(_data, stdr::begin(data));
    }

    // View of the string content, excluding the implicit trailing NUL.
    constexpr auto view() const -> StringView {
      return{ data, CAPACITY - 1u };
    }
    constexpr bool operator==(FixedString const&) const = default;
  };

  template <typename CharType, std::size_t SIZE>
  FixedString(CharType const (&)[SIZE]) -> FixedString<CharType, SIZE>;
}

namespace oxbox::utilities
{
  using detail::fixed_string::FixedString;
}