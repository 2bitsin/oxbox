#pragma once

// Matching a typed word against the names a command declares: its options,
// its subcommand members and its subcommand methods.

#include "oxbox/cli/command.hpp"
#include "oxbox/cli/member-role.hpp"
#include "oxbox/cli/naming.hpp"
#include "oxbox/cli/scheme.hpp"

#include <_buildutil/reflect.hpp>

#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace oxbox::cli::detail::name_lookup
{
  using namespace member_role;

  namespace naming = detail::naming;
  namespace scheme = detail::scheme;

  template <typename Item>
  constexpr auto MatchesShort(std::string_view typed) -> bool
  {
    for (auto const tag : Item::tags::VALUES)
      if (tag.starts_with("-") && tag == typed) return true;
    return false;
  }

  template <CommandDerived Owner>
  constexpr auto ClaimsShort(std::string_view typed) -> bool
  {
    bool claimed{ false };
    auto look = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, Owner>;
      if (MatchesShort<Item>(typed)) claimed = true;
    };
    scheme::ForEachItem<Owner>(look);
    return claimed;
  }

  template <CommandDerived Owner>
  auto OptionNames() -> std::vector<std::string_view>
  {
    std::vector<std::string_view> names;
    auto collect = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, Owner>;
      if constexpr (IsOption<Item, Owner>())
        names.push_back(naming::ExternalName<Item>());
    };
    scheme::ForEachItem<Owner>(collect);
    return names;
  }

  // When it does, the framework must not bind --help itself.
  template <CommandDerived Owner>
  consteval auto DeclaresHelp() -> bool
  {
    bool declared{ false };
    auto look = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, Owner>;
      if constexpr (IsOption<Item, Owner>() || IsSubcommand<Item, Owner>())
        if (naming::Matches(naming::ExternalName<Item>(), "help"))
          declared = true;
    };
    scheme::ForEachItem<Owner>(look);
    return declared;
  }

  inline constexpr std::size_t NO_DISPATCH{
    std::numeric_limits<std::size_t>::max() };

  // The index is into the flattened list, which the dispatch site folds too.
  template <CommandDerived Owner>
  auto FindSubcommand(std::string_view typed) -> std::size_t
  {
    auto found{ NO_DISPATCH };
    auto consider = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, Owner>;
      if constexpr (IsSubcommand<Item, Owner>())
        if (found == NO_DISPATCH
            && naming::Matches(naming::ExternalName<Item>(), typed))
          found = INDEX;
    };
    scheme::ForEachItem<Owner>(consider);
    return found;
  }

  // An unreflected interface is a quiet no, not a diagnostic.
  template <CommandDerived Owner>
  auto FindSubcommandMethod(std::string_view typed) -> std::size_t
  {
    if constexpr (!::reflect::interface_reflected<Owner>) {
      return NO_DISPATCH;
    } else {
      constexpr auto SCHEME{ ::reflect::interface_scheme_of<Owner>() };
      auto found{ NO_DISPATCH };
      auto consider = [&]<std::size_t INDEX>() {
        using Item = decltype(::reflect::scheme_item<INDEX>(SCHEME));
        if constexpr (IsSubcommandMethod<Item>())
          if (found == NO_DISPATCH
              && naming::Matches(naming::ExternalName<Item>(), typed))
            found = INDEX;
      };
      [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
        (consider.template operator()<INDEX>(), ...);
      }(std::make_index_sequence<::reflect::scheme_size(SCHEME)>{ });
      return found;
    }
  }
}

namespace oxbox::cli
{
  using detail::name_lookup::NO_DISPATCH;
  using detail::name_lookup::OptionNames;
}
