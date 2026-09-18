#pragma once

// How a C++ name becomes a command-line name: lowercase, underscores to
// dashes, for members, enumerators and subcommands alike. Matching compares
// through the transform, so no transformed name is ever materialised.

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <vector>

namespace oxbox::cli::detail::naming
{
  namespace stdr = std::ranges;

  // The label when one was given, verbatim, so `_Label(--)` can be a marker.
  template <typename Item>
  consteval auto ExternalName() -> std::string_view
  {
    return Item::LABEL.empty() ? Item::NAME_STRING : Item::LABEL;
  }

  constexpr auto NameChar(char value) noexcept -> char
  {
    if (value == '_') return '-';
    return (value >= 'A' && value <= 'Z')
      ? static_cast<char>(value - 'A' + 'a')
      : value;
  }

  constexpr auto Matches(std::string_view declared,
                         std::string_view typed) noexcept -> bool
  {
    return stdr::equal(declared, typed, [](char lhs, char rhs) {
      return NameChar(lhs) == NameChar(rhs);
    });
  }

  inline auto Spell(std::string_view declared) -> std::string
  {
    return stdr::to<std::string>(declared | std::views::transform(NameChar));
  }

  // Levenshtein distance, bounded by the cutoff.
  constexpr auto Distance(std::string_view lhs, std::string_view rhs,
                          std::size_t cutoff) -> std::size_t
  {
    auto const difference = lhs.size() > rhs.size()
      ? lhs.size() - rhs.size() : rhs.size() - lhs.size();
    if (difference > cutoff) return cutoff + 1u;

    std::vector<std::size_t> previous(rhs.size() + 1u);
    std::vector<std::size_t> current(rhs.size() + 1u);
    for (std::size_t index{ 0u }; index <= rhs.size(); ++index)
      previous[index] = index;

    for (std::size_t left{ 1u }; left <= lhs.size(); ++left) {
      current[0] = left;
      for (std::size_t right{ 1u }; right <= rhs.size(); ++right) {
        auto const same{ NameChar(lhs[left - 1u]) == NameChar(rhs[right - 1u]) };
        current[right] = std::min({ previous[right] + 1u,
                                    current[right - 1u] + 1u,
                                    previous[right - 1u] + (same ? 0u : 1u) });
      }
      std::swap(previous, current);
    }
    return previous[rhs.size()];
  }

  // The cutoff scales with length: "a" and "b" are 1 apart yet unrelated.
  inline auto Closest(std::string_view typed,
                      std::vector<std::string_view> const& declared)
    -> std::string_view
  {
    auto const cutoff{ std::max<std::size_t>(1u, typed.size() / 3u + 1u) };
    std::string_view best{ };
    auto found{ cutoff + 1u };
    for (auto const& candidate : declared) {
      auto const distance{ Distance(typed, candidate, cutoff) };
      if (distance < found) { found = distance; best = candidate; }
    }
    return found <= cutoff ? best : std::string_view{ };
  }
}

namespace oxbox::cli
{
  using detail::naming::Closest;
  using detail::naming::ExternalName;
  using detail::naming::Matches;
  using detail::naming::Spell;
}
