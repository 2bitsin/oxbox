#pragma once
// Generic text work: reshaping a view and reading a value out of one. Text
// in, text out -- nothing here reads a buffer or knows a format, and none of
// it throws.

#include <cctype>
#include <charconv>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>

namespace oxbox::utilities::detail::text
{
  using namespace std::string_view_literals;

  constexpr auto WHITESPACE{ " \t\r\n"sv };

  // ASCII case folding only: a Turkish locale would fold 'I' to 'ı' and
  // break every identifier comparison this exists for.
  inline auto Lowered(std::string_view input) -> std::string
  {
    auto const fold = [](char letter) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    };
    return input | std::views::transform(fold) | std::ranges::to<std::string>();
  }

  inline auto Trimmed(std::string_view input,
                      std::string_view cut = WHITESPACE) noexcept -> std::string_view
  {
    auto const begin{ input.find_first_not_of(cut) };
    if (begin == std::string_view::npos)
      return { };
    return input.substr(begin, input.find_last_not_of(cut) - begin + 1);
  }

  // The whole view or nothing: "12abc" read as 12 is a corrupt input silently
  // accepted, and an empty view is nullopt because absent and zero differ.
  template <typename _Number>
  auto WholeNumber(std::string_view input, int base = 10) -> std::optional<_Number>
  {
    _Number value{ };
    auto const stop{ input.data() + input.size() };
    auto const parsed{ std::from_chars(input.data(), stop, value, base) };
    if (input.empty() || parsed.ec != std::errc{ } || parsed.ptr != stop)
      return std::nullopt;
    return value;
  }
}

namespace oxbox::utilities
{
  using detail::text::Lowered;
  using detail::text::Trimmed;
  using detail::text::WHITESPACE;
  using detail::text::WholeNumber;
}
