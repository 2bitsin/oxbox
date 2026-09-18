#pragma once

// The guard that two of a command's declarations never answer to one
// command-line name; tools/negative-compile.sh asserts its wording.

#include "oxbox/cli/command.hpp"
#include "oxbox/cli/member-role.hpp"
#include "oxbox/cli/naming.hpp"
#include "oxbox/cli/scheme.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <vector>

namespace oxbox::cli::detail::one_name_per_declaration
{
  using namespace member_role;

  namespace naming = detail::naming;
  namespace scheme = detail::scheme;
  namespace stdr   = std::ranges;

  // The matcher stops at the first match, so a duplicate name is a dead member.
  template <CommandDerived Owner>
  consteval auto CollidingName() -> std::string_view
  {
    std::vector<std::string_view> named;
    auto collect = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, Owner>;
      if constexpr (IsOption<Item, Owner>() || IsSubcommand<Item, Owner>())
        named.push_back(naming::ExternalName<Item>());
    };
    scheme::ForEachItem<Owner>(collect);

    // Through the transform, because that is how the matcher compares.
    for (std::size_t left{ 0u }; left < named.size(); ++left)
      for (std::size_t right{ left + 1u }; right < named.size(); ++right)
        if (naming::Matches(named[left], named[right]))
          return named[right];
    return { };
  }

  // A static_assert message may be any constexpr data()/size() expression.
  template <CommandDerived Owner>
  struct DistinctNames
  {
    static constexpr std::string_view HEAD{
      "two of this command's declarations answer to the same command-line "
      "name, '--" };
    static constexpr std::string_view REST{
      "'. One of them is DEAD: the matcher takes the first in flattened "
      "order -- bases first, recursively, then the type's own -- so the "
      "other can never be written to, and the help screen lists two rows "
      "under one name. The command is on the instantiation line above. A "
      "LABEL REPLACES A NAME rather than adding one, which is how a label "
      "collides with an ordinary member, or with another label, or with a "
      "subcommand: options and subcommand members are one vocabulary. "
      "Rename one of the two." };

    static constexpr auto SAID{ [] {
      constexpr auto COLLIDING{ CollidingName<Owner>() };
      std::array<char, HEAD.size() + COLLIDING.size() + REST.size()> out{ };
      auto at{ stdr::copy(HEAD, out.begin()).out };
      at = stdr::transform(COLLIDING, at, naming::NameChar).out;
      stdr::copy(REST, at);
      return out;
    }() };

    static constexpr std::string_view MESSAGE{ SAID.data(), SAID.size() };
  };

  // msvc 19.51 lacks P2741R3: a message expression fails to parse.
  template <auto>
  inline constexpr bool NEVER{ false };

  template <CommandDerived Owner>
  inline constexpr auto COLLIDING_SPELLING{ [] {
    constexpr auto SAID{ CollidingName<Owner>() };
    std::array<char, SAID.size() + 1u> out{ };   // NUL-terminated: it reads
    stdr::transform(SAID, out.begin(), naming::NameChar);   // as a string in
    return out;                                  // gcc's and clang's dumps
  }() };

  template <auto NAME>
  struct OneNamePerDeclaration
  {
    static_assert(NEVER<NAME>,
      "two of this command's declarations answer to the same command-line "
      "name -- the one spelled by the template argument just above. One of "
      "them is DEAD: the matcher takes the first in flattened order -- bases "
      "first, recursively, then the type's own -- so the other can never be "
      "written to, and the help screen lists two rows under one name. The "
      "command is on the instantiation line above. A LABEL REPLACES A NAME "
      "rather than adding one, which is how a label collides with an "
      "ordinary member, or with another label, or with a subcommand: options "
      "and subcommand members are one vocabulary. Rename one of the two.");
  };
}
