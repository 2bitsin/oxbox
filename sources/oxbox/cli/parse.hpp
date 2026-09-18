#pragma once

// Command line to filled-in command object, driven by the reflected scheme:
// an option's name is the member name or its label, its arity is the member's
// type, and its default is whatever the freshly-constructed object holds.

#include "oxbox/cli/command.hpp"
#include "oxbox/cli/convert.hpp"
#include "oxbox/cli/errors.hpp"
#include "oxbox/cli/escaping.hpp"
#include "oxbox/cli/member-role.hpp"
#include "oxbox/cli/member-shape.hpp"
#include "oxbox/cli/name-lookup.hpp"
#include "oxbox/cli/naming.hpp"
#include "oxbox/cli/one-name-per-declaration.hpp"
#include "oxbox/cli/rest-collector.hpp"
#include "oxbox/cli/scheme.hpp"

#include <_buildutil/reflect.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <format>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace oxbox::cli::detail::parse
{
  using namespace escaping;
  using namespace member_role;
  using namespace member_shape;
  using namespace name_lookup;
  using namespace one_name_per_declaration;
  using namespace rest_collector;

  namespace naming = detail::naming;
  namespace scheme = detail::scheme;

  // A dispatch is reported, never performed; the dispatch site recurses.
  struct Outcome
  {
    bool help_requested{ false };

    // in order, values consumed by options excluded
    std::vector<std::string_view> positionals{ };

    // Never in `positionals`; both modes must decide the same split.
    std::vector<std::string_view> tail{ };

    // The collector's occurrence: no sentinel leaves the member at its default.
    bool sentinel_seen{ false };

    std::size_t subcommand_member{ NO_DISPATCH };  // flattened index
    std::size_t subcommand_method{ NO_DISPATCH };  // interface index
    std::size_t rest_at{ 0u };                     // where the sub's line starts

    [[nodiscard]] constexpr auto Dispatches() const noexcept -> bool
    {
      return subcommand_member != NO_DISPATCH
          || subcommand_method != NO_DISPATCH;
    }
  };

  // A null `into` is a scan: same walk, same refusals, writing nothing.
  template <CommandDerived Owner>
  auto ParseOptions(Owner* into, std::vector<std::string_view> const& args)
    -> Outcome
  {
    // Being pointed at this walk is what says the type is meant as a command.
    static_assert(scheme::HasMemberList<Owner>,
      "this type is being read as a command -- the root, a subcommand, or "
      "a layer on the way to one -- and it has no reflected scheme, so it "
      "has no options, no help rows and no subcommands to find. The type "
      "is named in the instantiation trace above. A class opts in from "
      "INSIDE itself with one line, `friend constexpr auto "
      "reflect_scheme(YourType*);`, and buildutil's reflect generator "
      "writes the scheme -- and the call scheme for its operator() -- from "
      "the declarations and the comments beside them. If that line IS "
      "there, the generated header for it was never reached: check that "
      "the type's header lives under sources/ and is included by its own "
      "path spelling, which is what resolves to the generated detour.");

    // tools/negative-compile.sh asserts the wording of both branches.
#if defined(__cpp_static_assert) && __cpp_static_assert >= 202306L
    if constexpr (!CollidingName<Owner>().empty())
      static_assert(false, DistinctNames<Owner>::MESSAGE);
#else
    if constexpr (!CollidingName<Owner>().empty())
      [[maybe_unused]] OneNamePerDeclaration<COLLIDING_SPELLING<Owner>> refused{ };
#endif

    static_assert(REST_COLLECTOR<Owner>.count <= 1u,
      "this command declares more than one rest collector -- more than one "
      "member annotated `_Label(--)`. The command is named in the "
      "instantiation trace above. There is one tail on a line, so exactly "
      "one member may claim it; the others want ordinary options, or "
      "positionals on operator().");

    // Flattened, so seen[] is indexed by the same index the matcher folds with.
    constexpr auto COUNT{ scheme::ItemCountOf<Owner> };

    std::array<std::size_t, COUNT ? COUNT : 1u> seen{ };
    Outcome outcome{ };

    for (std::size_t at{ 0u }; at < args.size(); ++at) {
      auto const argument{ args[at] };

      // Past the sentinel a token is only routed, never read.
      if (outcome.sentinel_seen) {
        if constexpr (HAS_REST_COLLECTOR<Owner>)
          outcome.tail.push_back(argument);
        else
          outcome.positionals.push_back(argument);
        continue;
      }

      if (!argument.starts_with("-") || argument == "-") {
        // A bare word triggers a method only in the first positional slot.
        if (outcome.positionals.empty()) {
          auto const method{ FindSubcommandMethod<Owner>(argument) };
          if (method != NO_DISPATCH) {
            outcome.subcommand_method = method;
            outcome.rest_at = at + 1u;
            return outcome;
          }
        }
        outcome.positionals.push_back(argument);
        continue;
      }

      auto const short_form{ !argument.starts_with("--") };
      auto body{ short_form ? argument : argument.substr(2u) };
      if (body.empty()) { outcome.sentinel_seen = true; continue; }

      // Splitting on '=' first would read --map:key=value as "map:key".
      auto const colon { FindUnescaped(body, ':') };
      auto const equals{ FindUnescaped(body, '=') };
      auto const entry_form{ colon != std::string_view::npos
                             && colon < equals };
      auto const has_inline{ !entry_form
                             && equals != std::string_view::npos };

      std::string_view inline_value{ };
      std::string_view entry_list  { };
      if (entry_form) {
        entry_list = body.substr(colon + 1u);
        body = body.substr(0u, colon);
      } else if (has_inline) {
        inline_value = body.substr(equals + 1u);
        body = body.substr(0u, equals);
      }

      // The first decisive token wins: `--help --bogus` is a help screen.
      if (!DeclaresHelp<Owner>()
          && (short_form ? body == "-h" && !ClaimsShort<Owner>("-h")
                         : naming::Matches("help", body))) {
        outcome.help_requested = true;
        outcome.rest_at = at + 1u;
        return outcome;
      }

      auto const subcommand{ short_form ? NO_DISPATCH
                                        : FindSubcommand<Owner>(body) };
      if (subcommand != NO_DISPATCH) {
        if (entry_form || has_inline)
          throw SubcommandTakesNoValue{ naming::Spell(body) };
        outcome.subcommand_member = subcommand;
        outcome.rest_at = at + 1u;
        return outcome;
      }

      bool matched{ false };
      auto consider = [&]<std::size_t INDEX>() {
        using Item = scheme::ItemAt<INDEX, Owner>;
        if constexpr (IsOption<Item, Owner>()) {
          if (matched || !(short_form ? MatchesShort<Item>(body)
              : naming::Matches(naming::ExternalName<Item>(), body)))
            return;
          matched = true;

          // From the scheme, not the object: a scan has no object.
          using Slot   = std::remove_reference_t<
            decltype(::reflect::member_of<Item>(std::declval<Owner&>()))>;
          using Member = std::remove_cv_t<Slot>;
          constexpr auto ARITY{ ArityOf<Member>() };
          auto const spelled{ naming::Spell(naming::ExternalName<Item>()) };

          // The one place the modes differ: a scan carries a null slot.
          Slot* slot{ nullptr };
          if (into != nullptr)
            slot = std::addressof(::reflect::member_of<Item>(*into));

          if constexpr (MapLike<Member>) {
            if (!entry_form)
              throw MalformedMapping{ spelled,
                "is a mapping and is written '--name:key=value'" };
            for (auto const entry : SplitUnescaped(entry_list, ',')) {
              auto const split{ FindUnescaped(entry, '=') };
              if (split == std::string_view::npos)
                throw MalformedMapping{ spelled,
                  std::format("entry '{}' is not 'key=value'", entry) };
              auto key{ convert::FromText<typename Member::key_type>(
                Unescaped(entry.substr(0u, split)), spelled) };
              auto value{ convert::FromText<typename Member::mapped_type>(
                Unescaped(entry.substr(split + 1u)), spelled) };
              // Last one wins: a repeated key reads as a correction.
              if (slot != nullptr)
                slot->insert_or_assign(std::move(key), std::move(value));
              ++seen[INDEX];
            }

          } else {
            if (entry_form)
              throw MalformedMapping{ spelled,
                "is not a mapping and takes no ':key=value' entries" };

            if constexpr (std::same_as<Member, bool>) {
              // The opposite of its default, and never eats the next argument.
              if (++seen[INDEX] > ARITY)
                throw RepeatedOption{ spelled, ARITY };
              if (has_inline) {
                auto const given{ convert::FromText<bool>(inline_value,
                                                          spelled) };
                if (slot != nullptr) *slot = given;
              } else if (slot != nullptr) {
                *slot = !*slot;
              }

            } else {
              // A scalar spends its slot on the occurrence, before the value.
              constexpr bool SCALAR{ !ListLike<Member> && !ArrayLike<Member> };
              if constexpr (SCALAR) {
                if (++seen[INDEX] > ARITY)
                  throw RepeatedOption{ spelled, ARITY };
              }

              std::string_view text{ inline_value };
              if (!has_inline) {
                if (at + 1u >= args.size()) throw MissingValue{ spelled };
                text = args[++at];
              }

              if constexpr (ListLike<Member>) {
                auto const pieces{ SplitUnescaped(text, ',') };
                for (auto const piece : pieces) {
                  auto value{ convert::FromText<typename Member::value_type>(
                    Unescaped(piece), spelled) };
                  if (slot != nullptr) Append(*slot, std::move(value));
                }
                seen[INDEX] += pieces.size();

              } else if constexpr (ArrayLike<Member>) {
                auto const pieces{ SplitUnescaped(text, ',') };
                if (seen[INDEX] + pieces.size() > ARITY)
                  throw RepeatedOption{ spelled, ARITY };
                for (auto const piece : pieces) {
                  auto value{ convert::FromText<typename Member::value_type>(
                    Unescaped(piece), spelled) };
                  if (slot != nullptr) (*slot)[seen[INDEX]] = std::move(value);
                  ++seen[INDEX];
                }

              } else {
                // No unescaping: a scalar has no separator to hide from.
                auto value{ convert::FromText<Member>(text, spelled) };
                if (slot != nullptr) *slot = std::move(value);
              }
            }
          }
        }
      };
      scheme::ForEachItem<Owner>(consider);

      if (!matched) {
        // Both names are spelled: a suggestion is something to retype.
        auto const declared{ OptionNames<Owner>() };
        if (short_form) throw UnknownOption{ body, { } };
        throw UnknownOption{ naming::Spell(body),
                             naming::Spell(naming::Closest(body, declared)) };
      }
    }

    // Only on the way out: a segment that ended early returned above.
    if constexpr (HAS_REST_COLLECTOR<Owner>) {
      if (into != nullptr && outcome.sentinel_seen)
        AssignRest(*into, outcome.tail);
    }
    return outcome;
  }

  template <CommandDerived Owner>
  auto ParseOptions(Owner& into, std::vector<std::string_view> const& args)
    -> Outcome
  {
    return ParseOptions(&into, args);
  }

  // Judge the line with no object: a question about a type and a line.
  template <CommandDerived Owner>
  auto ScanOptions(std::vector<std::string_view> const& args) -> Outcome
  {
    return ParseOptions(static_cast<Owner*>(nullptr), args);
  }
}

namespace oxbox::cli
{
  using detail::parse::Outcome;
  using detail::parse::ParseOptions;
  using detail::parse::ScanOptions;
}
