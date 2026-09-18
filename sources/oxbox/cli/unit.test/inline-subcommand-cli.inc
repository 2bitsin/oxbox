#pragma once

// .inc, not .hpp: buildutil's reflect generator claims any header that
// spells `reflect_scheme` and emits its own, which would collide with the
// hand-written schemes below -- and those are the point.

#include "oxbox/cli/main.hpp"

#include <array>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <vector>

namespace oxbox::cli::inline_test
{
  namespace stdr = std::ranges;

  // what ran, in order, and what it was given
  struct Trace
  {
    std::vector<std::string> steps { };   // Initializes and actions
    std::string              word  { };
    int                      number{ 0 };
    std::array<int, 2>       pair  { };
    std::vector<std::string> rest  { };
    std::string              label { };   // the parent's option, as the
                                          // leaf found it

    // what the middle layer is told to do with the torch
    int                      mid_code{ 0 };
  };

  // Main parses into a default-constructed Command::Get<App>(), so there
  // is no constructor argument to hand a recorder in through.
  inline auto Seen() -> Trace&
  {
    static thread_local Trace trace{ };
    return trace;
  }

  // a nested layer, so descent reaches an inline leaf through two steps
  struct Mid : Command
  {
    friend constexpr auto reflect_scheme(Mid*);
    friend constexpr auto reflect_interface_scheme(Mid*);

    int volume{ 0 };                     /* the middle layer's own option */

    auto Initialize() -> CliResult
    {
      Seen().steps.push_back("mid");
      if (Seen().mid_code != 0)
        return CliResult::Failed(Seen().mid_code, "the middle refused");
      return{ };
    }

    auto murmur(std::string word) const -> CliResult
    {
      Seen().steps.push_back("murmur:" + word);
      Seen().number = volume;
      return{ 29 };
    }
  };

  // the application, carrying all three spellings at once
  struct App : Command
  {
    friend constexpr auto reflect_scheme(App*);
    friend constexpr auto reflect_interface_scheme(App*);

    std::string label{ "none" };         /* the application's own option */
    Mid         mid  { };                /* a nested layer, by member */

    auto Initialize() -> CliResult
    {
      Seen().steps.push_back("app");
      return{ };
    }

    // one scalar positional, the shape the shorthand exists for
    auto chat(std::string say_what) const -> CliResult
    {
      Seen().steps.push_back("chat:" + say_what);
      Seen().label = label;
      return{ 11 };
    }

    // every parameter shape at once
    auto blend(std::array<int, 2>           pair,
               std::tuple<std::string, int> both,
               RangeView<std::string>       the_rest) const -> CliResult
    {
      Seen().steps.push_back("blend");
      Seen().pair   = pair;
      Seen().word   = std::get<0>(both);
      Seen().number = std::get<1>(both);
      Seen().rest   = stdr::to<std::vector<std::string>>(the_rest);
      return{ 13 };
    }

    // one that is needed and one that may be left out, in the only order
    // the signature rule allows
    auto maybe(std::string first, std::optional<int> second) const
      -> CliResult
    {
      Seen().steps.push_back("maybe:" + first);
      Seen().number = second.value_or(-1);
      return{ 17 };
    }

    // public and reflected, and not a subcommand: it answers an int
    auto status() const -> int { return 7; }

    // the factory spelling, so the three live side by side
    auto reach_mid() const -> Mid& { return Command::Get<Mid>(); }
  };
}

// Hand-written, to keep this file independent of the generator, in the
// shapes buildutil emits; Initialize is listed as the generator lists it.
namespace oxbox::cli::inline_test
{
  constexpr auto reflect_scheme(Mid*)
  {
    using T = Mid;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"volume", &T::volume,
                               "the middle layer's own option", false>>{ };
  }

  constexpr auto reflect_interface_scheme(Mid*)
  {
    using T = Mid;
    return ::reflect::interface_scheme<
      ::reflect::method_scheme<"Initialize", &T::Initialize,
        "brings the middle up",
        ::reflect::call_scheme<>>,
      ::reflect::method_scheme<"murmur", &T::murmur, "say it quietly",
        ::reflect::call_scheme<
          ::reflect::param_scheme<"word", "what to murmur">>>>{ };
  }

  constexpr auto reflect_scheme(App*)
  {
    using T = App;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"label", &T::label, "the application's own option", false>,
      ::reflect::member_scheme<"mid",   &T::mid,   "a nested layer",               false>>{ };
  }

  // no reflect_call_scheme(App*): the application is not a verb

  constexpr auto reflect_interface_scheme(App*)
  {
    using T = App;
    return ::reflect::interface_scheme<
      ::reflect::method_scheme<"Initialize", &T::Initialize,
        "brings the application up",
        ::reflect::call_scheme<>>,
      ::reflect::method_scheme<"blend", &T::blend, "every shape at once",
        ::reflect::call_scheme<
          ::reflect::param_scheme<"pair",     "exactly two">,
          ::reflect::param_scheme<"both",     "a word and a number">,
          ::reflect::param_scheme<"the_rest", "whatever is left">>>,
      ::reflect::method_scheme<"chat", &T::chat, "say one thing and stop",
        ::reflect::call_scheme<
          ::reflect::param_scheme<"say_what", "the words to say">>>,
      ::reflect::method_scheme<"maybe", &T::maybe,
        "one word, and another if you like",
        ::reflect::call_scheme<
          ::reflect::param_scheme<"first",  "the one that is needed">,
          ::reflect::param_scheme<"second", "and one that is not">>>,
      ::reflect::method_scheme<"reach_mid", &T::reach_mid,
        "the factory spelling",
        ::reflect::call_scheme<>>,
      ::reflect::method_scheme<"status", &T::status, "an ordinary accessor",
        ::reflect::call_scheme<>>>{ };
  }
}
