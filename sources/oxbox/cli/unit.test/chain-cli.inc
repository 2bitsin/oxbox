#pragma once

// .inc, not .hpp: buildutil's reflect generator claims any header that
// spells `reflect_scheme` and emits its own, which would collide with the
// hand-written schemes below -- and those are the point.

#include "oxbox/cli/main.hpp"

#include <optional>
#include <string>
#include <vector>

namespace oxbox::cli::chain_test
{
  // what every layer wrote down, and what the test told the root to do
  struct Trace
  {
    std::vector<std::string> steps      { };      // Initializes and
                                                  // actions, in order
    int                      factories  { 0 };    // how often a child was
                                                  // asked for

    // a layer refuses the torch by answering non-zero; one per depth, so
    // that "the descent stops here" can be pinned at each of them
    int                      root_code  { 0 };
    std::string              root_reason{ };
    bool                     root_throws{ false };
    int                      mid_code   { 0 };
    int                      doer_code  { 0 };
  };

  // Main<Root> parses into a default-constructed Command::Get<Root>(), so
  // there is no constructor argument to hand a recorder in through.
  inline auto Chain() -> Trace&
  {
    static thread_local Trace trace{ };
    return trace;
  }

  // every test starts from an empty sequence and a root told to succeed
  inline auto Fresh() -> void { Chain() = Trace{ }; }

  // ── the layers ────────────────────────────────────────────────────────

  // nothing but an action: operator() and no Initialize, and 17 is its own
  // answer, distinctive enough not to be confused with a layer's success
  struct Leaf : Command
  {
    friend constexpr auto reflect_scheme(Leaf*);
    friend constexpr auto reflect_call_scheme(Leaf*);

    std::string tone{ "plain" };         /* the leaf's own option */

    auto operator() (std::optional<std::string> word /* what to say */) const
      -> CliResult
    {
      Chain().steps.push_back("leaf:" + word.value_or("-"));
      return{ 17 };
    }
  };

  // a destination that has both: something to bring up, and something to do
  struct Doer : Command
  {
    friend constexpr auto reflect_scheme(Doer*);
    friend constexpr auto reflect_call_scheme(Doer*);

    auto Initialize() -> CliResult
    {
      Chain().steps.push_back("doer-init");
      if (Chain().doer_code != 0)
        return CliResult::Failed(Chain().doer_code, "the doer was not ready");
      return{ };
    }

    auto operator() (std::optional<std::string> word /* what to do */) const
      -> CliResult
    {
      Chain().steps.push_back("doer:" + word.value_or("-"));
      return{ 23 };
    }
  };

  // the middle: an Initialize and no operator(), the shape of a layer that
  // exists to be passed through
  struct Mid : Command
  {
    friend constexpr auto reflect_scheme(Mid*);

    int  level{ 0 };                     /* the middle layer's own option */
    Leaf leaf { };                       /* and its own subcommand */

    auto Initialize() -> CliResult
    {
      Chain().steps.push_back("mid");
      if (Chain().mid_code != 0)
        return CliResult::Failed(Chain().mid_code, "the middle refused");
      return{ };
    }
  };

  // nothing to bring up: no Initialize, so it is skipped rather than refused
  struct Group : Command
  {
    friend constexpr auto reflect_scheme(Group*);

    Leaf leaf{ };                        /* the only thing it is for */
  };

  // the application: it brings the run up and declares no operator()
  struct Root : Command
  {
    friend constexpr auto reflect_scheme(Root*);
    friend constexpr auto reflect_interface_scheme(Root*);

    std::string endpoint{ "none" };      /* the application's own option */
    Mid         mid     { };             /* a subcommand by member */
    Group       group   { };             /* a grouping layer */
    Doer        doer    { };             /* brings itself up, then acts */

    // the method form of the same descent, asked for the child only after
    // this command's own Initialize has succeeded
    auto forge_mid() const -> Mid
    {
      ++Chain().factories;
      return Mid{ };
    }

    auto Initialize() -> CliResult
    {
      Chain().steps.push_back("root");
      if (Chain().root_throws)
        throw utilities::ParseError{ "could not connect to api" };
      if (Chain().root_code != 0)
        return CliResult::Failed(Chain().root_code, Chain().root_reason);
      return{ };
    }
  };

  // an operator() and a subcommand at once: passed through it does not act,
  // reached as the destination the same operator() does
  struct Eager : Command
  {
    friend constexpr auto reflect_scheme(Eager*);
    friend constexpr auto reflect_call_scheme(Eager*);

    Leaf leaf{ };                        /* somewhere further to go */

    auto Initialize() -> CliResult
    {
      Chain().steps.push_back("eager-init");
      return{ };
    }

    auto operator() () const -> CliResult
    {
      Chain().steps.push_back("eager-action");
      return{ 41 };
    }
  };

  // two spellings of one subcommand, on a root that can be followed down
  struct Speller : Command
  {
    friend constexpr auto reflect_scheme(Speller*);
    friend constexpr auto reflect_interface_scheme(Speller*);

    Leaf& by_switch{ Command::Get<Leaf>() };  /* the dashed spelling */

    auto by_word() const -> Leaf& { return Command::Get<Leaf>(); }

    auto Initialize() -> CliResult
    {
      Chain().steps.push_back("speller");
      return{ };
    }
  };
}

// Hand-written, in the shapes buildutil emits. Not one of these schemes
// mentions Initialize: the framework asks the type whether the call
// compiles rather than reading a scheme, and leaving it out proves that.
namespace oxbox::cli::chain_test
{
  constexpr auto reflect_scheme(Leaf*)
  {
    using T = Leaf;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"tone", &T::tone, "the leaf's own option",
                               false>>{ };
  }

  constexpr auto reflect_call_scheme(Leaf*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"word", "what to say">>{ };
  }

  constexpr auto reflect_scheme(Doer*)
  {
    return ::reflect::class_scheme<>{ };
  }

  constexpr auto reflect_call_scheme(Doer*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"word", "what to do">>{ };
  }

  constexpr auto reflect_scheme(Mid*)
  {
    using T = Mid;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"level", &T::level, "the middle layer's own option", false>,
      ::reflect::member_scheme<"leaf",  &T::leaf,  "and its own subcommand",        false>>{ };
  }

  // no reflect_call_scheme(Mid*): a layer has no operator() to reflect

  constexpr auto reflect_scheme(Eager*)
  {
    using T = Eager;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"leaf", &T::leaf, "somewhere further to go",
                               false>>{ };
  }

  constexpr auto reflect_call_scheme(Eager*)
  {
    return ::reflect::call_scheme<>{ };
  }

  constexpr auto reflect_scheme(Group*)
  {
    using T = Group;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"leaf", &T::leaf, "the only thing it is for",
                               false>>{ };
  }

  // no reflect_call_scheme(Group*): the absence is the case under test

  constexpr auto reflect_scheme(Root*)
  {
    using T = Root;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"endpoint", &T::endpoint, "the application's own option", false>,
      ::reflect::member_scheme<"mid",      &T::mid,      "a subcommand by member",       false>,
      ::reflect::member_scheme<"group",    &T::group,    "a grouping layer",             false>,
      ::reflect::member_scheme<"doer",     &T::doer,     "brings itself up, then acts",  false>>{ };
  }

  // no reflect_call_scheme(Root*) either: the application is not a verb

  constexpr auto reflect_interface_scheme(Root*)
  {
    using T = Root;
    return ::reflect::interface_scheme<
      ::reflect::method_scheme<"forge_mid", &T::forge_mid,
                               "the method form of the same descent">>{ };
  }

  constexpr auto reflect_scheme(Speller*)
  {
    return ::reflect::class_scheme<
      // a reference member has no pointer-to-member, so the scheme names
      // it with an accessor, exactly as the generator emits
      ::reflect::member_scheme<"by_switch",
        [](auto&& owner) -> decltype(auto) { return (owner.by_switch); },
        "the dashed spelling", false>>{ };
  }

  constexpr auto reflect_interface_scheme(Speller*)
  {
    using T = Speller;
    return ::reflect::interface_scheme<
      ::reflect::method_scheme<"by_word", &T::by_word,
                               "the bare-word spelling">>{ };
  }
}
