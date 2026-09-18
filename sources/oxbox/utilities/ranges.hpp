#pragma once

#include <cstddef>
#include <tuple>
#include <utility>

namespace oxbox::utilities::detail::ranges
{
  template <std::size_t Index>
  inline constexpr auto get = 
    []<typename T>(T&& item) -> decltype(auto) {
      return std::get<Index>(
        std::forward<T>(item));
    };
} // namespace oxbox::utilities::detail::ranges

namespace oxbox::utilities
{
  using detail::ranges::get;
} // namespace oxbox::utilities
