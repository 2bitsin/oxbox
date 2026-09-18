#pragma once

// Positional arguments bound to a signature's parameters -- a command's
// operator() or an inline subcommand's method -- and Initialize, the
// chain step that takes none.

#include "oxbox/cli/command.hpp"
#include "oxbox/cli/convert.hpp"
#include "oxbox/cli/errors.hpp"
#include "oxbox/cli/member-role.hpp"
#include "oxbox/cli/member-shape.hpp"
#include "oxbox/cli/naming.hpp"
#include "oxbox/cli/parse.hpp"
#include "oxbox/cli/range.hpp"

#include <_buildutil/reflect.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace oxbox::cli::detail::invoke
{
  namespace naming = detail::naming;
  namespace parse  = detail::parse;
  namespace stdr   = std::ranges;

  template <typename Type>
  struct TailElement
  {
  };

  template <typename Element, stdr::subrange_kind KIND>
  struct TailElement<stdr::subrange<range::AnyIterator<Element>,
                                    range::AnyIterator<Element>, KIND>>
  {
    using type = Element;
  };

  template <typename Type>
  concept TailParameter =
    requires { typename TailElement<std::remove_cvref_t<Type>>::type; };

  template <typename Type>
  concept ShapedParameter =
    requires { std::tuple_size<std::remove_cvref_t<Type>>::value; };

  template <typename Type>
  struct OptionalElement
  {
  };

  template <typename Element>
  struct OptionalElement<std::optional<Element>>
  {
    using type = Element;
  };

  template <typename Type>
  concept OptionalParameter =
    requires { typename OptionalElement<std::remove_cvref_t<Type>>::type; };

  // How many tokens a parameter takes when it takes any.
  template <typename Param>
  consteval auto ConsumedBy() -> std::size_t
  {
    using Bare = std::remove_cvref_t<Param>;
    if constexpr (TailParameter<Bare>)
      return 0u;
    else if constexpr (OptionalParameter<Bare>)
      return ConsumedBy<typename OptionalElement<Bare>::type>();
    else if constexpr (ShapedParameter<Bare>)
      return std::tuple_size_v<Bare>;
    else
      return 1u;
  }

  template <typename Type>
  concept ContainerParameter =
    !TailParameter<Type> &&
    !ShapedParameter<Type> &&
    !std::same_as<std::remove_cvref_t<Type>, std::string> &&
    requires(std::remove_cvref_t<Type>& into,
             typename std::remove_cvref_t<Type>::value_type value) {
      into.begin(); into.end(); into.insert(into.end(), value); };

  // The one shaped parameter that is not all-or-nothing: it fills a prefix.
  template <typename Type>
  concept OptionalArrayParameter =
    ShapedParameter<Type> &&
    requires { typename std::remove_cvref_t<Type>::value_type; } &&
    OptionalParameter<typename std::remove_cvref_t<Type>::value_type>;

  inline constexpr std::size_t UNBOUNDED_INTAKE{
    std::numeric_limits<std::size_t>::max() };

  template <typename Param>
  consteval auto MaxIntake() -> std::size_t
  {
    using Bare = std::remove_cvref_t<Param>;
    if constexpr (TailParameter<Bare> || ContainerParameter<Bare>)
      return UNBOUNDED_INTAKE;
    else if constexpr (OptionalParameter<Bare>)
      return ConsumedBy<typename OptionalElement<Bare>::type>();
    else if constexpr (ShapedParameter<Bare>)
      return std::tuple_size_v<Bare>;
    else
      return 1u;
  }

  // Min zero is what "optional" means here; OptionalsComeLast is stated in it.
  template <typename Param>
  consteval auto MinIntake() -> std::size_t
  {
    using Bare = std::remove_cvref_t<Param>;
    if constexpr (TailParameter<Bare> || ContainerParameter<Bare>
               || OptionalParameter<Bare> || OptionalArrayParameter<Bare>)
      return 0u;
    else if constexpr (ShapedParameter<Bare>)
      return std::tuple_size_v<Bare>;
    else
      return 1u;
  }

  template <typename Parameters, std::size_t ... INDEX>
  consteval auto MinEach(std::index_sequence<INDEX...>)
    -> std::array<std::size_t, sizeof...(INDEX)>
  {
    return { MinIntake<std::tuple_element_t<INDEX, Parameters>>()... };
  }

  // No parameter with min > 0 may follow one with min 0: that is what lets
  // the greedy walk run without lookahead. And no nested lambda in here --
  // a lambda inside a fold expression ICEs gcc 16 (tsubst, cp/pt.cc:17282).
  template <typename Parameters>
  consteval auto OptionalsComeLast() -> bool
  {
    constexpr auto COUNT{ std::tuple_size_v<Parameters> };
    if constexpr (COUNT == 0u) {
      return true;
    } else {
      auto const mins{
        MinEach<Parameters>(std::make_index_sequence<COUNT>{ }) };
      bool optional_seen{ false };
      for (std::size_t at{ 0u }; at < COUNT; ++at) {
        if (optional_seen && mins[at] > 0u) return false;
        if (mins[at] == 0u) optional_seen = true;
      }
      return true;
    }
  }

  template <typename Param>
  auto Assemble(std::vector<std::string_view> const& positionals,
                std::size_t from, std::string_view named) -> Param
  {
    auto build = [&]<std::size_t ... ELEMENT>(std::index_sequence<ELEMENT...>) {
      return Param{ convert::FromText<std::remove_cvref_t<
        std::tuple_element_t<ELEMENT, Param>>>(
          positionals[from + ELEMENT], named)... };
    };
    return build(std::make_index_sequence<std::tuple_size_v<Param>>{ });
  }

  template <typename Value>
  auto Build(std::vector<std::string_view> const& positionals,
             std::size_t from, std::string_view named) -> Value
  {
    if constexpr (ShapedParameter<Value>)
      return Assemble<Value>(positionals, from, named);
    else
      return convert::FromText<Value>(positionals[from], named);
  }

  // Advances the cursor by whatever the parameter took, so correctness
  // depends on the caller building the arguments strictly in order.
  template <typename Param>
  auto Take(std::vector<std::string_view> const& positionals,
            std::size_t& cursor, std::string_view declared,
            std::string_view about) -> Param
  {
    using Bare = std::remove_cvref_t<Param>;

    auto const named    { naming::Spell(declared) };
    auto const available{ positionals.size() - cursor };

    if constexpr (TailParameter<Bare>) {
      auto const from{ std::next(positionals.begin(),
                                 static_cast<std::ptrdiff_t>(cursor)) };
      cursor = positionals.size();
      return range::EraseRange<typename TailElement<Bare>::type>(
        from, positionals.end());

    } else if constexpr (ContainerParameter<Bare>) {
      // The token is passed straight through: materialising a temporary
      // string here would leave a value_type of string_view dangling.
      Bare into{ };
      while (cursor < positionals.size())
        member_shape::Append(into, convert::FromText<typename Bare::value_type>(
          positionals[cursor++], named));
      return into;

    } else if constexpr (OptionalParameter<Bare>) {
      using Element = typename OptionalElement<Bare>::type;
      constexpr auto NEEDED{ ConsumedBy<Element>() };

      // Whole or not at all: falling short of an indivisible capacity is
      // nullopt, not an error.
      if (available < NEEDED)
        return Bare{ };

      auto const from{ cursor };
      cursor += NEEDED;
      return Bare{ Build<Element>(positionals, from, named) };

    } else if constexpr (OptionalArrayParameter<Bare>) {
      using Element = typename Bare::value_type;
      using Inner   = typename OptionalElement<Element>::type;
      constexpr auto CAPACITY{ std::tuple_size_v<Bare> };

      Bare into{ };
      for (std::size_t at{ 0u };
           at < CAPACITY && cursor < positionals.size(); ++at)
        into[at] = Element{
          convert::FromText<Inner>(positionals[cursor++], named) };
      return into;

    } else {
      constexpr auto NEEDED{ ConsumedBy<Bare>() };
      if (available < NEEDED)
        throw MissingArgument{ named, about };

      auto const from{ cursor };
      cursor += NEEDED;
      return Build<Bare>(positionals, from, named);
    }
  }

  // The parameter types come from the language, their names and comments
  // from the generator.
  template <typename PARAMETERS, typename CALL>
  struct Signature
  {
    using Parameters = PARAMETERS;
    using Call       = CALL;
  };

  using NoParameters = Signature<std::tuple<>, ::reflect::call_scheme<>>;

  // Public, not overloaded, not a template -- what &App::operator() asks.
  template <typename App>
  concept HasCallOperator = requires { &App::operator(); };

  // Members reflected but operator() not is a stale scheme, and the action
  // would silently disappear behind a help screen.
  template <typename App>
  concept ActionIsVisible =
    ::reflect::callable_reflected<App>
    || !HasCallOperator<App>
    || !::reflect::reflected<App>;

  template <CommandDerived App, bool = ::reflect::callable_reflected<App>>
  struct CommandSignature
  {
    using type = NoParameters;
  };

  template <CommandDerived App>
  struct CommandSignature<App, true>
  {
    using type = Signature<
      typename ::reflect::call_traits_of<App>::template apply<std::tuple>,
      decltype(::reflect::call_scheme_of<App>())>;
  };

  template <CommandDerived App>
  using SignatureOf = typename CommandSignature<App>::type;

  template <typename Item>
  using MethodSignature = Signature<
    typename ::reflect::call_traits<decltype(Item::METHOD)>
      ::template apply<std::tuple>,
    typename Item::parameters>;

  // The one place a parameter's external name is read, so the usage line
  // and the refusals cannot disagree. The bounds check keeps BindTo's
  // arity assert the only thing said when a scheme is out of step.
  template <std::size_t INDEX, typename Sig>
  consteval auto ParamNameOf() -> std::string_view
  {
    constexpr typename Sig::Call CALL{ };
    if constexpr (INDEX < ::reflect::scheme_size(CALL)) {
      using Item = decltype(::reflect::scheme_item<INDEX>(CALL));
      static_assert(Item::LABEL != member_role::REST_MARKER,
        "the rest-collector marker `_Label(--)` is for ONE PUBLIC DATA "
        "MEMBER that is not itself a Command -- the member the tail after "
        "the sentinel is delivered to. AN operator() PARAMETER is not "
        "that: the tokens after the sentinel are never positionals, so "
        "this parameter would only be rendered as `<-->` and never "
        "filled from a tail. The parameter is on the instantiation line "
        "above. Give it a real label, or none -- and if what was wanted "
        "was the tail itself, declare a member for it instead.");
      return naming::ExternalName<Item>();
    } else {
      return { };
    }
  }

  template <std::size_t INDEX, typename Sig>
  consteval auto ParamCommentOf() -> std::string_view
  {
    constexpr typename Sig::Call CALL{ };
    if constexpr (INDEX < ::reflect::scheme_size(CALL))
      return decltype(::reflect::scheme_item<INDEX>(CALL))::COMMENT;
    else
      return { };
  }

  // Neither a command nor a method is a parameter here: binding reads the
  // signature and never an object, so it can be asked before anything exists.
  template <typename Sig>
  auto BindTo(std::vector<std::string_view> const& positionals)
    -> typename Sig::Parameters
  {
    using Parameters = typename Sig::Parameters;
    using Call       = typename Sig::Call;

    constexpr Call CALL { };
    constexpr auto COUNT{ std::tuple_size_v<Parameters> };

    static_assert(::reflect::scheme_size(CALL) == COUNT,
      "the reflected call scheme and the signature it names disagree about "
      "how many parameters there are. The parameter NAMES come from the "
      "generator and the TYPES come from the language, so this is a "
      "stale reflect header: rebuild, and check the signature the "
      "generator read is the one that compiles.");

    static_assert(OptionalsComeLast<Parameters>(),
      "once an optional parameter is hit, the remaining parameters must "
      "also be optional. Positional binding is one greedy left-to-right "
      "pass: a parameter that may take nothing takes whatever is there, "
      "so a REQUIRED parameter after it can be starved by an ordinary "
      "line -- and after an unbounded one (a RangeView, a vector or any "
      "other insertable container) it can never be filled at all. "
      "Optional here means intake starting at zero: std::optional<T>, "
      "std::array<std::optional<T>, N>, a container, or a RangeView. "
      "Move the required parameters ahead of them.");

    // Braced initialisation guarantees left-to-right evaluation, which the
    // walking cursor depends on; a constructor's arguments do not.
    std::size_t cursor{ 0u };
    auto arguments{
      [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
        return Parameters{ Take<std::tuple_element_t<INDEX, Parameters>>(
          positionals, cursor,
          ParamNameOf   <INDEX, Sig>(),
          ParamCommentOf<INDEX, Sig>())... };
      }(std::make_index_sequence<COUNT>{ }) };

    if (cursor < positionals.size())
      throw UnexpectedArgument{ positionals[cursor] };

    return arguments;
  }

  template <CommandDerived App> requires ::reflect::callable_reflected<App>
  auto Bind(std::vector<std::string_view> const& positionals)
    -> typename ::reflect::call_traits_of<App>::template apply<std::tuple>
  {
    return BindTo<SignatureOf<App>>(positionals);
  }

  // The whole binding walk with what it built thrown away: the run uses
  // the same code, so validating and running cannot disagree.
  template <typename Sig>
  auto ValidateTo(std::vector<std::string_view> const& positionals) -> void
  {
    [[maybe_unused]] auto const bound{ BindTo<Sig>(positionals) };
  }

  // The funnel every destination goes through, so ActionIsVisible is asked
  // here.
  template <CommandDerived App>
  auto Validate(std::vector<std::string_view> const& positionals) -> void
  {
    static_assert(ActionIsVisible<App>,
      "this command declares an operator() and has no reflected call "
      "scheme, so the framework cannot see its action: a line that ends "
      "here answers this command's help screen with a usage exit code "
      "instead of running it. The type is named in the instantiation trace "
      "above. The call scheme comes from the same opt-in the member scheme "
      "does, so a type reflected in one and not the other has a stale "
      "generated header -- rebuild. A hand-written scheme needs the second "
      "entry point written too: `friend constexpr auto "
      "reflect_call_scheme(YourType*);` beside the member one, and a "
      "definition to match. A command that is deliberately NOT a verb "
      "should not declare an operator() at all.");

    ValidateTo<SignatureOf<App>>(positionals);
  }

  // `auto Initialize() -> CliResult`, public and taking nothing.
  // Accessibility is part of the probe: a private one is not callable here.
  template <typename App>
  concept HasInitialize = requires (App& app) { app.Initialize(); };

  template <typename App>
  concept Initializes =
    HasInitialize<App> &&
    requires (App& app) {
      { app.Initialize() } -> std::convertible_to<CliResult>; };

  // A command without Initialize is skipped; one that answers the wrong
  // thing is refused rather than silently treated as having none.
  template <CommandDerived App>
  auto Initialize(App& app) -> CliResult
  {
    if constexpr (!HasInitialize<App>) {
      return CliResult{ };

    } else {
      static_assert(Initializes<App>,
        "this command declares Initialize but it does not answer a "
        "CliResult, so the framework cannot tell whether the world came "
        "up. Declare it `auto Initialize() -> CliResult` -- return{ } to "
        "pass the torch, or CliResult::Failed(code, \"why\") to stop the "
        "descent where it stands. `void` is not an answer: a step that "
        "cannot refuse is a step nothing below it can trust, and it would "
        "otherwise be mistaken for a command with no Initialize at all and "
        "never run.");
      return CliResult{ app.Initialize() };
    }
  }

  template <CommandDerived App> requires ::reflect::callable_reflected<App>
  auto Invoke(App& app, std::vector<std::string_view> const& positionals)
    -> CliResult
  {
    auto arguments{ Bind<App>(positionals) };

    using Result = decltype(std::apply(app, std::move(arguments)));
    if constexpr (std::is_void_v<Result>) {
      std::apply(app, std::move(arguments));
      return CliResult{ };
    } else {
      return CliResult{ std::apply(app, std::move(arguments)) };
    }
  }

  // The method is called on the owner: already filled, already initialized.
  template <typename Item, CommandDerived Owner>
  auto InvokeMethod(Owner& owner,
                    std::vector<std::string_view> const& positionals)
    -> CliResult
  {
    auto arguments{ BindTo<MethodSignature<Item>>(positionals) };

    return std::apply(
      [&owner](auto&& ... words) -> CliResult {
        return (owner.*(Item::METHOD))(
          std::forward<decltype(words)>(words)...);
      }, std::move(arguments));
  }
}

namespace oxbox::cli
{
  using detail::invoke::ActionIsVisible;
  using detail::invoke::Bind;
  using detail::invoke::BindTo;
  using detail::invoke::ContainerParameter;
  using detail::invoke::HasCallOperator;
  using detail::invoke::HasInitialize;
  using detail::invoke::Initialize;
  using detail::invoke::Initializes;
  using detail::invoke::Invoke;
  using detail::invoke::InvokeMethod;
  using detail::invoke::MaxIntake;
  using detail::invoke::MethodSignature;
  using detail::invoke::MinIntake;
  using detail::invoke::OptionalArrayParameter;
  using detail::invoke::OptionalParameter;
  using detail::invoke::OptionalsComeLast;
  using detail::invoke::Signature;
  using detail::invoke::SignatureOf;
  using detail::invoke::TailParameter;
  using detail::invoke::UNBOUNDED_INTAKE;
}
