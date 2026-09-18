#pragma once

#include <algorithm>
#include <concepts>
#include <iterator>
#include <ranges>
#include <string_view>
#include <string>
#include <type_traits>

namespace oxbox::utilities::detail::string
{
  namespace stdr = std::ranges;
  namespace stdv = stdr::views;

  inline constexpr auto _StringLikeValue(std::basic_string_view<char    >) noexcept -> std::true_type  { return{ }; }
  inline constexpr auto _StringLikeValue(std::basic_string_view<char8_t >) noexcept -> std::true_type  { return{ }; }
  inline constexpr auto _StringLikeValue(std::basic_string_view<wchar_t >) noexcept -> std::true_type  { return{ }; }
  inline constexpr auto _StringLikeValue(std::basic_string_view<char16_t>) noexcept -> std::true_type  { return{ }; }
  inline constexpr auto _StringLikeValue(std::basic_string_view<char32_t>) noexcept -> std::true_type  { return{ }; }
  inline constexpr auto _StringLikeValue(...                             ) noexcept -> std::false_type { return{ }; } 

  template <typename Value>
  concept StringLikeValue = 
    decltype(_StringLikeValue(std::declval<Value const&>()))::value;

  template <typename Sequence>
  concept StringSequence = (std::forward_iterator<Sequence> && 
    StringLikeValue<std::iter_value_t<Sequence>>);

  template <typename Range>
  concept StringRange = (stdr::borrowed_range<Range> && 
    StringLikeValue<stdr::range_value_t<Range>>);

  template <stdr::borrowed_range Range>
  using CompatibleStringView = 
    decltype(std::basic_string_view{ 
      std::declval<stdr::range_value_t<Range>>() 
    });
}

namespace oxbox::utilities
{
  using detail::string::CompatibleStringView;
  using detail::string::StringLikeValue;
  using detail::string::StringSequence;
  using detail::string::StringRange;
}