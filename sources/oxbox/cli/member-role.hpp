#pragma once

// What a reflected member or method IS to the command line: an option, a
// subcommand, the rest collector, or none of the three.

#include "oxbox/cli/command.hpp"

#include <_buildutil/reflect.hpp>

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace oxbox::cli::detail::member_role
{
  // A label, taken verbatim, and so never matched as an external name.
  inline constexpr std::string_view REST_MARKER{ "--" };

  template <typename Item, typename Owner>
  consteval auto IsRestCollector() -> bool
  {
    using Member = std::remove_cvref_t<
      decltype(::reflect::member_of<Item>(std::declval<Owner&>()))>;
    return !Item::ENCAPSULATED && !CommandDerived<Member>
        && Item::LABEL == REST_MARKER;
  }

  template <typename Item, typename Owner>
  consteval auto IsOption() -> bool
  {
    using Member = std::remove_cvref_t<
      decltype(::reflect::member_of<Item>(std::declval<Owner&>()))>;
    return !Item::ENCAPSULATED && !CommandDerived<Member>
        && !IsRestCollector<Item, Owner>();
  }

  template <typename Item, typename Owner>
  consteval auto IsSubcommand() -> bool
  {
    using Member = std::remove_cvref_t<
      decltype(::reflect::member_of<Item>(std::declval<Owner&>()))>;
    if constexpr (!Item::ENCAPSULATED && CommandDerived<Member>) {
      static_assert(Item::LABEL != REST_MARKER,
        "the rest-collector marker `_Label(--)` is for ONE PUBLIC DATA "
        "MEMBER that is not itself a Command -- the member the tail after "
        "the sentinel is delivered to. A SUBCOMMAND MEMBER is not that: "
        "it is reached BY NAME, and the marker is not a name, so this "
        "member would be spelled `----` on the help screen and matched by "
        "nothing the line can write. The member is on the instantiation "
        "line above. Give it a real label, or none.");
      return true;
    } else {
      return false;
    }
  }

  // The return type is read strictly, or an int accessor becomes a verb.
  enum class MethodRole : std::uint8_t
  {
    NONE,       // an ordinary method: not the command line's business
    DESCENDS,   // ask it for the child, then read the rest against that
    ACTS,       // it is the subcommand; bind the rest to its parameters
  };

  // invoke.hpp's hook, matched on the declared spelling, not lowercased.
  inline constexpr std::string_view LIFECYCLE_HOOK{ "Initialize" };

  template <typename Item>
  consteval auto RoleOf() -> MethodRole
  {
    if constexpr (!requires { typename ::reflect::call_traits<
                    decltype(Item::METHOD)>::result; }) {
      return MethodRole::NONE;
    } else {
      using Traits = ::reflect::call_traits<decltype(Item::METHOD)>;
      using Result = typename Traits::result;

      if constexpr (Traits::ARITY == 0u && CommandDerived<Result>)
        return MethodRole::DESCENDS;
      else if constexpr (std::same_as<Result, CliResult>)
        return Item::NAME_STRING == LIFECYCLE_HOOK ? MethodRole::NONE
                                                   : MethodRole::ACTS;
      else
        return MethodRole::NONE;
    }
  }

  template <typename Item>
  consteval auto IsSubcommandMethod() -> bool
  {
    if constexpr (RoleOf<Item>() != MethodRole::NONE) {
      static_assert(Item::LABEL != REST_MARKER,
        "the rest-collector marker `_Label(--)` is for ONE PUBLIC DATA "
        "MEMBER that is not itself a Command -- the member the tail after "
        "the sentinel is delivered to. A SUBCOMMAND METHOD is not that: "
        "it is reached BY A BARE WORD, and the marker is not a word, so "
        "this method could only be triggered by writing `--`, which ends "
        "option reading instead. The method is on the instantiation line "
        "above. Give it a real label, or none.");
      return true;
    } else {
      return false;
    }
  }

}

namespace oxbox::cli
{
  using detail::member_role::IsSubcommandMethod;
  using detail::member_role::MethodRole;
  using detail::member_role::REST_MARKER;
  using detail::member_role::RoleOf;
}
