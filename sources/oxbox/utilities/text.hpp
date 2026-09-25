#pragma once

#include <algorithm>
#include <cctype>
#include <concepts>
#include <format>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace oxbox::utilities::detail::text
{
  using namespace std::string_view_literals;

  constexpr auto WHITESPACE{ " \t\r\n\v\f"sv };

  template <typename _Type>
  concept CharRange = std::ranges::input_range<_Type>
                   && std::same_as<std::ranges::range_value_t<_Type>, char>;

  template <typename _Type>
    requires (std::convertible_to<_Type, std::string_view> && !CharRange<_Type>)
  constexpr auto JoinElement(_Type&& value)
  {
    using Text = std::conditional_t<std::is_lvalue_reference_v<_Type> || std::is_pointer_v<_Type>,
                                    std::string_view, std::string>;
    return Text{ std::string_view{ value } };
  }

  template <CharRange _Type>
  constexpr auto JoinElement(_Type&& value)
  {
    using Range = std::conditional_t<std::is_lvalue_reference_v<_Type>,
                                     std::ranges::ref_view<std::remove_reference_t<_Type>>,
                                     std::remove_cvref_t<_Type>>;
    return std::views::all(Range{ std::forward<_Type>(value) });
  }

  template <typename _Type>
    requires (!std::convertible_to<_Type, std::string_view> && !CharRange<_Type>)
  auto JoinElement(_Type&& value) -> std::string
  { return std::format("{}", value); }

  template <std::ranges::input_range _Range, typename _Projection>
  constexpr auto Joined(_Range&& range, std::string_view separator, _Projection projection)
    -> std::string
  {
    auto elements{ std::ranges::ref_view{ range } | std::views::transform(std::move(projection))
                   | std::views::transform([](auto&& value) {
                       return JoinElement(std::forward<decltype(value)>(value)); }) };
    auto append = [separator, first = true](std::string joined, auto&& element) mutable {
      if (!std::exchange(first, false))
        joined.append(separator);
      joined.append(std::forward<decltype(element)>(element) | std::ranges::to<std::string>());
      return joined;
    };
    return std::ranges::fold_left(elements, std::string{ }, std::move(append));
  }

  template <std::ranges::input_range _Range>
  constexpr auto Joined(_Range&& range, std::string_view separator) -> std::string
  { return Joined(std::forward<_Range>(range), separator, std::identity{ }); }

  // ASCII case folding only: a Turkish locale would fold 'I' to 'ı' and
  // break every identifier comparison this exists for.
  inline auto Lowered(std::string_view input) -> std::string
  {
    auto const fold = [](char letter) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    };
    return input | std::views::transform(fold) | std::ranges::to<std::string>();
  }

  inline constexpr auto Trimmed(std::string_view input,
                                std::string_view cut = WHITESPACE) noexcept -> std::string_view
  {
    auto const begin{ input.find_first_not_of(cut) };
    if (begin == std::string_view::npos)
      return { };
    return input.substr(begin, input.find_last_not_of(cut) - begin + 1);
  }
}

namespace oxbox::utilities
{
  using detail::text::Joined;
  using detail::text::Lowered;
  using detail::text::Trimmed;
  using detail::text::WHITESPACE;
}
