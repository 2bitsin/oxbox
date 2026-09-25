#pragma once

// Finding the `_Label(--)` member, and delivering the tail after the
// sentinel to it.

#include "oxbox/cli/command.hpp"
#include "oxbox/cli/member-role.hpp"
#include "oxbox/cli/scheme.hpp"

#include <_buildutil/reflect.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <string_view>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace oxbox::cli::detail::rest_collector
{
  using namespace member_role;

  namespace scheme = detail::scheme;
  namespace stdr   = std::ranges;

  enum class RestKind : std::uint8_t
  {
    NONE,
    TOKENS,   // std::vector<std::string>      -- one per token, owned
    VIEWS,    // std::vector<std::string_view> -- one per token, borrowed
    JOINED,   // std::string                   -- one line, single spaces
  };

  template <typename Member>
  consteval auto RestKindOf() -> RestKind
  {
    if constexpr (std::same_as<Member, std::vector<std::string>>)
      return RestKind::TOKENS;
    else if constexpr (std::same_as<Member, std::vector<std::string_view>>)
      return RestKind::VIEWS;
    else if constexpr (std::same_as<Member, std::string>)
      return RestKind::JOINED;
    else
      return RestKind::NONE;
  }

  // A class template on the item, so the instantiation line names the member.
  template <typename Item, typename Member>
  struct RestCollectorType
  {
    static constexpr auto KIND{ RestKindOf<Member>() };

    static_assert(KIND != RestKind::NONE,
      "this member carries the rest-collector marker `_Label(--)` and is "
      "not a type the tail can be delivered in. The member and its type "
      "are on the instantiation line above. Accepted: "
      "std::vector<std::string> -- the tokens, owned; "
      "std::vector<std::string_view> -- the tokens, viewed into the "
      "argument storage the caller keeps alive, exactly as a string_view "
      "option member borrows it; and std::string -- the tokens joined "
      "with single spaces, which cannot round-trip a token that contains "
      "one. Anything else has no reading here: the tail is whole tokens "
      "in order, and is never split, converted or counted.");

    // Naming it instantiates the template, which fires the assert above.
    static constexpr bool CHECKED{ true };
  };

  inline constexpr std::size_t NO_COLLECTOR{
    std::numeric_limits<std::size_t>::max() };

  struct RestCollector
  {
    std::size_t index{ NO_COLLECTOR };
    std::size_t count{ 0u };
  };

  template <CommandDerived Owner>
  consteval auto RestCollectorOf() -> RestCollector
  {
    RestCollector found{ };
    auto consider = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, Owner>;
      if constexpr (IsRestCollector<Item, Owner>()) {
        using Member = std::remove_cvref_t<
          decltype(::reflect::member_of<Item>(std::declval<Owner&>()))>;
        static_assert(RestCollectorType<Item, Member>::CHECKED);
        if (found.count == 0u) found.index = INDEX;
        ++found.count;
      }
    };
    scheme::ForEachItem<Owner>(consider);
    return found;
  }

  template <CommandDerived Owner>
  inline constexpr RestCollector REST_COLLECTOR{ RestCollectorOf<Owner>() };

  template <CommandDerived Owner>
  inline constexpr bool HAS_REST_COLLECTOR{
    REST_COLLECTOR<Owner>.index != NO_COLLECTOR };

  // Nothing is read on the way: no splitting, unescaping, conversion or arity.
  template <CommandDerived Owner>
  auto AssignRest(Owner& into, std::vector<std::string_view> const& tail)
    -> void
  {
    auto assign = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, Owner>;
      if constexpr (IsRestCollector<Item, Owner>()) {
        using Member = std::remove_cvref_t<
          decltype(::reflect::member_of<Item>(std::declval<Owner&>()))>;
        constexpr auto KIND{ RestKindOf<Member>() };
        auto& slot{ ::reflect::member_of<Item>(into) };

        if constexpr (KIND == RestKind::JOINED) {
          // Lossy: a token with a space is indistinguishable from two.
          slot = std::ranges::fold_left(
            tail, std::string{},
            [](std::string joined, std::string_view token) {
              if (!joined.empty()) joined += ' ';
              joined += token;
              return joined;
            });

        } else if constexpr (KIND != RestKind::NONE) {
          slot.assign(tail.begin(), tail.end());
        }
      }
    };
    scheme::ForEachItem<Owner>(assign);
  }
}

namespace oxbox::cli
{
  using detail::rest_collector::HAS_REST_COLLECTOR;
  using detail::rest_collector::NO_COLLECTOR;
  using detail::rest_collector::RestKind;
}
