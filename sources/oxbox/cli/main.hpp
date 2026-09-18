#pragma once

// The entry points. Main takes a command type and a command line; Apply
// makes two passes over that line -- judge the whole of it over types
// alone, then fill, initialize, and descend or act.

#include "oxbox/cli/command.hpp"
#include "oxbox/cli/errors.hpp"
#include "oxbox/cli/help.hpp"
#include "oxbox/cli/invoke.hpp"
#include "oxbox/cli/member-role.hpp"
#include "oxbox/cli/name-lookup.hpp"
#include "oxbox/cli/parse.hpp"
#include "oxbox/cli/range.hpp"
#include "oxbox/cli/scheme.hpp"

#include "oxbox/utilities/errors.hpp"
#include "oxbox/utilities/string.hpp"

#include <concepts>
#include <cstddef>
#include <exception>
#include <iterator>
#include <optional>
#include <print>
#include <ranges>
#include <string_view>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace oxbox::cli::detail::main
{
  namespace stdv   = std::ranges::views;
  namespace scheme = detail::scheme;
  using namespace utilities;

  // Exact for every const spelling of argv: a qualification conversion
  // loses to the counted-sequence overload, which then reads argv[0].
  template <typename Type>
  using Pointee = std::remove_cv_t<
    std::remove_pointer_t<std::remove_cvref_t<Type>>>;

  template <typename Type>
  concept ArgumentVector =
    std::is_pointer_v<std::remove_cvref_t<Type>> &&
    std::is_pointer_v<Pointee<Type>> &&
    StringLikeValue<Pointee<Type>>;

  template <StringRange InputRange>
  auto Collect(InputRange&& input_range) -> std::vector<std::string_view>
  {
    std::vector<std::string_view> out;
    for (auto&& item : input_range)
      out.emplace_back(std::string_view{ item });
    return out;
  }

  // Throws on user misuse: the ParseError family, or TypeMismatch.
  template <CommandDerived CliApp>
  auto Apply(CliApp& into, std::vector<std::string_view> const& args,
             std::string_view program = { }) -> CliResult;

  // Pass two, reached only through Apply and only for an approved line.
  template <CommandDerived CliApp>
  auto Execute(CliApp& into, std::vector<std::string_view> const& args)
    -> CliResult;

  // An answer means the line is already decided; malformed never gets to
  // be a value at all, it leaves as a diagnostic.
  struct Verdict
  {
    std::optional<CliResult> answer{ };

    [[nodiscard]] auto Answered() const noexcept -> bool
    {
      return answer.has_value();
    }
  };

  template <typename Item, CommandDerived Owner, CommandDerived ... Ancestors>
  auto ValidateInline(std::vector<std::string_view> const& args,
                      std::vector<std::string_view> const& trail,
                      std::string_view program) -> Verdict
  {
    auto const outcome{ parse::ScanOptions<command::InlineSegment>(args) };

    if (outcome.help_requested)
      return Verdict{ CliResult::HelpShown(
        help::FormatInlineHelp<Item, Owner, Ancestors...>(trail, program)) };

    invoke::ValidateTo<invoke::MethodSignature<Item>>(outcome.positionals);
    return Verdict{ };
  }

  // `Ancestors` is the chain above this layer, root first; `trail` is the
  // same chain in trigger words, and the two are kept in step.
  template <CommandDerived Owner, CommandDerived ... Ancestors>
  auto Validate(std::vector<std::string_view> const& args,
                std::vector<std::string_view> const& trail,
                std::string_view program = { }) -> Verdict
  {
    static_assert((!std::same_as<Owner, Ancestors> && ...),
      "this command is its own ancestor: the subcommand graph has a "
      "cycle, and a walk that judges the whole line cannot name every "
      "layer of a chain that never ends. A subcommand may appear under "
      "as many DIFFERENT parents as it likes -- what it may not do is "
      "sit anywhere below itself. Break the cycle: hold the inner "
      "occurrence privately (an encapsulated member is not a "
      "subcommand), or restate the two commands so that neither "
      "contains the other.");

    auto const outcome{ parse::ScanOptions<Owner>(args) };

    if (outcome.help_requested)
      return Verdict{ CliResult::HelpShown(
        help::FormatChainHelp<Owner, Ancestors...>(trail, program)) };

    if (!outcome.Dispatches()) {
      // Asked before the fall to help below: a stray word is named, and
      // only a line with nothing left over gets the screen.
      invoke::Validate<Owner>(outcome.positionals);

      if constexpr (!::reflect::callable_reflected<Owner>)
        return Verdict{ CliResult::NoAction(
          help::FormatChainHelp<Owner, Ancestors...>(trail, program)) };
      else
        return Verdict{ };
    }

    if (!outcome.positionals.empty())
      throw UnexpectedArgument{ outcome.positionals.front() };

    std::vector<std::string_view> const rest(
      std::next(args.begin(), static_cast<std::ptrdiff_t>(outcome.rest_at)),
      args.end());

    std::vector<std::string_view> deeper{ trail };
    deeper.push_back(args[outcome.rest_at - 1u]);

    Verdict verdict{ };
    if (outcome.subcommand_member != name_lookup::NO_DISPATCH) {
      auto look = [&]<std::size_t INDEX>() {
        using Item = scheme::ItemAt<INDEX, Owner>;
        if constexpr (member_role::IsSubcommand<Item, Owner>())
          if (INDEX == outcome.subcommand_member)
            verdict = Validate<std::remove_cvref_t<decltype(
              ::reflect::member_of<Item>(std::declval<Owner&>()))>,
              Ancestors..., Owner>(rest, deeper, program);
      };
      scheme::ForEachItem<Owner>(look);

    } else if constexpr (::reflect::interface_reflected<Owner>) {
      constexpr auto SCHEME{ ::reflect::interface_scheme_of<Owner>() };
      // Neither method is called: a factory's return type is named, an
      // inline subcommand's parameters are read off its scheme.
      auto look = [&]<std::size_t INDEX>() {
        using Item = decltype(::reflect::scheme_item<INDEX>(SCHEME));
        constexpr auto ROLE{ member_role::RoleOf<Item>() };

        if constexpr (ROLE == member_role::MethodRole::DESCENDS) {
          if (INDEX == outcome.subcommand_method)
            verdict = Validate<std::remove_cvref_t<typename ::reflect::
              call_traits<decltype(Item::METHOD)>::result>,
              Ancestors..., Owner>(rest, deeper, program);

        } else if constexpr (ROLE == member_role::MethodRole::ACTS) {
          if (INDEX == outcome.subcommand_method)
            verdict = ValidateInline<Item, Owner, Ancestors...>(
              rest, deeper, program);
        }
      };
      [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
        (look.template operator()<INDEX>(), ...);
      }(std::make_index_sequence<::reflect::scheme_size(SCHEME)>{ });
    }
    return verdict;
  }

  // `which` indexes the flattened member list, so a subcommand a base
  // declares is entered like any other.
  template <CommandDerived Owner>
  auto EnterSubcommand(Owner& into, std::size_t which,
                       std::vector<std::string_view> const& rest) -> CliResult
  {
    CliResult result{ };
    auto enter = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, Owner>;
      if constexpr (member_role::IsSubcommand<Item, Owner>())
        if (INDEX == which)
          result = Execute(::reflect::member_of<Item>(into), rest);
    };
    scheme::ForEachItem<Owner>(enter);
    return result;
  }

  // A factory is called after the parent's Initialize succeeded; its
  // result is bound by reference so a parent-owned child is not copied.
  template <CommandDerived Owner>
  auto EnterSubcommandMethod(Owner& into, std::size_t which,
                             std::vector<std::string_view> const& rest)
    -> CliResult
  {
    CliResult result{ };
    if constexpr (::reflect::interface_reflected<Owner>) {
      constexpr auto SCHEME{ ::reflect::interface_scheme_of<Owner>() };
      auto enter = [&]<std::size_t INDEX>() {
        using Item = decltype(::reflect::scheme_item<INDEX>(SCHEME));
        constexpr auto ROLE{ member_role::RoleOf<Item>() };

        if constexpr (ROLE == member_role::MethodRole::DESCENDS) {
          if (INDEX == which) {
            auto&& subcommand{ (into.*(Item::METHOD))() };
            result = Execute(subcommand, rest);
          }

        } else if constexpr (ROLE == member_role::MethodRole::ACTS) {
          if (INDEX == which) {
            // `outcome` outlives the call: a tail parameter is a view
            // over its positionals.
            auto const outcome{
              parse::ScanOptions<command::InlineSegment>(rest) };
            result = invoke::InvokeMethod<Item>(into, outcome.positionals);
          }
        }
      };
      [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
        (enter.template operator()<INDEX>(), ...);
      }(std::make_index_sequence<::reflect::scheme_size(SCHEME)>{ });
    }
    return result;
  }

  template <CommandDerived CliApp>
  auto Execute(CliApp& into, std::vector<std::string_view> const& args)
    -> CliResult
  {
    auto const outcome{ parse::ParseOptions(into, args) };

    // Pass one refuses this line before it gets here; the check stays
    // because Execute is a step that has to hold on its own terms.
    if (outcome.Dispatches() && !outcome.positionals.empty())
      throw UnexpectedArgument{ outcome.positionals.front() };

    // Settled above the Initialize below, because nothing may be brought
    // up on the way to a screen.
    if constexpr (!::reflect::callable_reflected<CliApp>) {
      if (!outcome.Dispatches()) {
        invoke::Validate<CliApp>(outcome.positionals);
        return CliResult::NoAction(help::FormatHelp<CliApp>());
      }
    }

    // A parent initializes before its child parses, destination included.
    if (auto step{ invoke::Initialize(into) }; !step.Ok())
      return step;

    // `outcome` keeps the positionals alive: a tail parameter is a view.
    if constexpr (::reflect::callable_reflected<CliApp>) {
      if (!outcome.Dispatches())
        return invoke::Invoke(into, outcome.positionals);
    }

    std::vector<std::string_view> const rest(
      std::next(args.begin(), static_cast<std::ptrdiff_t>(outcome.rest_at)),
      args.end());
    return outcome.subcommand_member != name_lookup::NO_DISPATCH
      ? EnterSubcommand(into, outcome.subcommand_member, rest)
      : EnterSubcommandMethod(into, outcome.subcommand_method, rest);
  }

  // Pass one runs once, over the whole line; the recursion below is pass
  // two's alone.
  template <CommandDerived CliApp>
  auto Apply(CliApp& into, std::vector<std::string_view> const& args,
             std::string_view program) -> CliResult
  {
    std::vector<std::string_view> const nowhere_yet{ };
    if (auto verdict{ Validate<CliApp>(args, nowhere_yet, program) };
        verdict.Answered())
      return std::move(*verdict.answer);

    return Execute(into, args);
  }

  // Constrained away from argv: a pointer-to-pointer must not reach an
  // overload that reads from element zero.
  template <CommandDerived CliApp, StringRange InputRange>
    requires (!ArgumentVector<InputRange>)
  auto Main(CliApp&& cliapp, InputRange&& input_range,
            std::string_view program = { }) -> CliResult
  {
    auto const args{ Collect(std::forward<InputRange>(input_range)) };
    // Only user misuse is mapped; a command's own exception passes through.
    // TypeMismatch is deliberately not a ParseError (utilities/errors.hpp).
    auto const result{ [&] {
      try {
        return Apply(cliapp, args, program);
      } catch (utilities::ParseError const& failure) {
        return CliResult::UsageError(failure.what());
      } catch (utilities::TypeMismatch const& failure) {
        return CliResult::UsageError(failure.what());
      }
    }() };

    switch (result.Status()) {
      case CliStatus::HELP_SHOWN:
      case CliStatus::NO_ACTION:
        std::print("{}", result.Message());
        break;
      case CliStatus::USAGE_ERROR:
        std::print(stderr, "error: {}\n", result.Message());
        break;
      case CliStatus::RAN:
        if (!result.Message().empty())
          std::print(stderr, "error: {}\n", result.Message());
        break;
    }
    return result;
  }

  // `length` elements from the cursor, all of them arguments: no program
  // name is skipped.
  template <CommandDerived CliApp, std::integral Length, StringSequence InputSeq>
    requires (!ArgumentVector<InputSeq>)
  auto Main(CliApp&& cliapp, Length length, InputSeq input_seq) -> CliResult
  {
    return Main(std::forward<CliApp>(cliapp), stdv::counted(input_seq, length));
  }

  // argv[0] is skipped as an argument and kept as the program name the
  // help screens speak of the root by.
  template <CommandDerived CliApp, std::integral Length, ArgumentVector Argv>
  auto Main(CliApp&& cliapp, Length argc, Argv argv) -> CliResult
  {
    auto const given{ argc > Length{ 0 } ? static_cast<std::size_t>(argc)
                                         : 0u };
    auto const count{ given > 0u ? given - 1u : 0u };

    std::string_view program{ };
    if (given > 0u && argv[0] != nullptr) {
      program = std::string_view{ argv[0] };
      // npos + 1 is 0, so a bare name is kept whole.
      program = program.substr(program.find_last_of("/\\") + 1u);
    }

    return Main(std::forward<CliApp>(cliapp),
                stdv::counted(argv + (given > 0u ? 1 : 0), count), program);
  }

  // The root is its own singleton: Get<Root>() hands back the object Main
  // parsed into, and a second run starts where the first left off.
  template <CommandDerived CliApp, typename ... ForwardArgs>
  auto Main(ForwardArgs&& ... fwdargs) -> CliResult
  {
    return Main(Command::Get<CliApp>(),
                std::forward<ForwardArgs>(fwdargs)...);
  }
}

namespace oxbox::cli
{
  using detail::range::AnyView;
  using detail::main::Apply;
  using detail::main::ArgumentVector;
  using detail::main::EnterSubcommand;
  using detail::main::EnterSubcommandMethod;
  using detail::main::Execute;
  using detail::main::Collect;
  using detail::main::Main;
  using detail::main::Validate;
  using detail::main::ValidateInline;
  using detail::main::Verdict;
  using detail::range::RangeView;
}
