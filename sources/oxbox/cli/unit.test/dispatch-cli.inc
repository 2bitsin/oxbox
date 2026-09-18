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

namespace oxbox::cli::unit_test
{
  namespace stdr = std::ranges;

  struct Trace
  {
    std::string              ran   { };   // which operator() ran
    std::string              word  { };
    int                      number{ 0 };
    int                      extra { 0 };
    std::array<int, 2>       pair  { };
    std::vector<std::string> rest  { };
    std::string              label { };   // what the parent's option said
  };

  enum class Shade : int { LOUD = 1, SOFT };
  constexpr auto reflect_scheme(Shade*);

  // Every parameter shape at once: a scalar, a fixed-extent one, a
  // heterogeneous one, and the tail.
  struct Echo : Command
  {
    friend constexpr auto reflect_scheme(Echo*);
    friend constexpr auto reflect_call_scheme(Echo*);

    explicit Echo(Trace* trace = nullptr) : _trace{ trace } { }

    int leading{ 0 };                    /* an option, not a positional */

    auto operator() (int                          count,     /* how many */
                     std::array<int, 2>           pair,      /* exactly two */
                     std::tuple<std::string, int> both,      /* a word and a number */
                     RangeView<std::string>       the_rest   /* whatever is left */
                    ) const -> CliResult
    {
      _trace->ran    = "echo";
      _trace->number = count;
      _trace->pair   = pair;
      _trace->word   = std::get<0>(both);
      _trace->extra  = std::get<1>(both);
      _trace->rest   = stdr::to<std::vector<std::string>>(the_rest);
      return{ 11 };
    }

  private:
    Trace* _trace;
  };

  // No tail, so it can run out of arguments and be given too many.
  struct Greet : Command
  {
    friend constexpr auto reflect_scheme(Greet*);
    friend constexpr auto reflect_call_scheme(Greet*);

    explicit Greet(Trace* trace = nullptr) : _trace{ trace } { }

    bool twice{ false };                 /* say it again */

    auto operator() (std::string who      /* whom to greet */,
                     Shade       in_shade /* which shade to greet in */
                    ) const -> CliResult
    {
      _trace->ran    = "greet";
      _trace->word   = who;
      _trace->number = static_cast<int>(in_shade);
      return{ 5 };
    }

  private:
    Trace* _trace;
  };

  struct Silent : Command
  {
    friend constexpr auto reflect_scheme(Silent*);
    friend constexpr auto reflect_call_scheme(Silent*);

    explicit Silent(Trace* trace = nullptr) : _trace{ trace } { }

    auto operator() (std::string word /* what was said */) const -> void
    {
      _trace->ran  = "silent";
      _trace->word = word;
    }

  private:
    Trace* _trace;
  };

  struct Numeric : Command
  {
    friend constexpr auto reflect_scheme(Numeric*);
    friend constexpr auto reflect_call_scheme(Numeric*);

    Numeric() = default;

    auto operator() (int code /* what to exit with */) const -> int
    {
      return code;
    }
  };

  struct Sub : Command
  {
    friend constexpr auto reflect_scheme(Sub*);
    friend constexpr auto reflect_call_scheme(Sub*);

    explicit Sub(Trace* trace = nullptr) : _trace{ trace } { }

    int depth{ 0 };                      /* only the subcommand has this */

    auto operator() (std::string word /* what to say */) const -> CliResult
    {
      _trace->ran    = "sub";
      _trace->word   = word;
      _trace->number = depth;
      return{ 3 };
    }

  private:
    Trace* _trace;
  };

  struct Root : Command
  {
    friend constexpr auto reflect_scheme(Root*);
    friend constexpr auto reflect_call_scheme(Root*);
    friend constexpr auto reflect_interface_scheme(Root*);

    explicit Root(Trace* trace = nullptr)
    : nested{ trace }, _trace{ trace }
    { }

    std::string label { "none" };        /* the parent's own option */
    Sub         nested;                  /* a subcommand */

    // Zero parameters, returns a Command: a subcommand, reached by a bare
    // word. It is asked of the parent after the options are in, which is
    // what lets the child be built from them. Named in the members'
    // spelling rather than the project's, because the naming rule that
    // turns it into `perform-action` is the members' one.
    auto perform_action() const -> Sub
    {
      _trace->label = label;
      return Sub{ _trace };
    }

    // Reflected, public, zero parameters -- and not a Command, so not a
    // subcommand. A word that names it is an ordinary positional.
    auto helper() const -> int { return 0; }

    // The step. It is what makes this command passable-through, and it is
    // where the label the subcommand goes on to read is written -- the
    // options are already in by the time it runs, which is the whole
    // reason a parent may build things from them.
    auto Initialize() -> CliResult
    {
      _trace->label = label;
      return{ };
    }

    // The action, which runs only when the line stops here. It is
    // parent-shaped only because this fixture wants to reach it with and
    // without a word; the framework no longer demands anything of a
    // dispatching layer's signature, because a dispatching layer's
    // operator() is never called.
    auto operator() (std::optional<std::string> only     /* the parent's own */,
                     RangeView<std::string>     the_rest /* and the remainder */
                    ) const -> CliResult
    {
      _trace->ran   = "root";
      _trace->word  = only.value_or("");
      _trace->label = label;
      _trace->rest  = stdr::to<std::vector<std::string>>(the_rest);
      return only ? CliResult{ 7 } : CliResult{ };
    }

  private:
    Trace* _trace;
  };
}

// Hand-written to keep this test independent of the generator; the shapes
// match what buildutil emits (see the generated header for hello-world).
namespace oxbox::cli::unit_test
{
  constexpr auto reflect_scheme(Shade*)
  {
    return ::reflect::enum_scheme<
      ::reflect::enumerator<"LOUD", Shade::LOUD, "at volume">,
      ::reflect::enumerator<"SOFT", Shade::SOFT, "under the breath">>{ };
  }

  constexpr auto reflect_scheme(Echo*)
  {
    using T = Echo;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"leading", &T::leading, "an option", false>,
      ::reflect::member_scheme<"_trace",  &T::_trace,  "",          true >>{ };
  }

  constexpr auto reflect_call_scheme(Echo*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"count",    "how many">,
      ::reflect::param_scheme<"pair",     "exactly two">,
      ::reflect::param_scheme<"both",     "a word and a number">,
      ::reflect::param_scheme<"the_rest", "whatever is left">>{ };
  }

  constexpr auto reflect_scheme(Greet*)
  {
    using T = Greet;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"twice",  &T::twice,  "say it again", false>,
      ::reflect::member_scheme<"_trace", &T::_trace, "",             true >>{ };
  }

  constexpr auto reflect_call_scheme(Greet*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"who",      "whom to greet">,
      ::reflect::param_scheme<"in_shade", "which shade to greet in">>{ };
  }

  constexpr auto reflect_scheme(Silent*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_trace", &Silent::_trace, "", true>>{ };
  }

  constexpr auto reflect_call_scheme(Silent*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"word", "what was said">>{ };
  }

  constexpr auto reflect_scheme(Numeric*)
  {
    return ::reflect::class_scheme<>{ };
  }

  constexpr auto reflect_call_scheme(Numeric*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"code", "what to exit with">>{ };
  }

  constexpr auto reflect_scheme(Sub*)
  {
    using T = Sub;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"depth",  &T::depth,  "only the subcommand has this", false>,
      ::reflect::member_scheme<"_trace", &T::_trace, "",                             true >>{ };
  }

  constexpr auto reflect_call_scheme(Sub*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"word", "what to say">>{ };
  }

  constexpr auto reflect_scheme(Root*)
  {
    using T = Root;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"label",  &T::label,  "the parent's own option", false>,
      ::reflect::member_scheme<"nested", &T::nested, "a subcommand",            false>,
      ::reflect::member_scheme<"_trace", &T::_trace, "",                        true >>{ };
  }

  constexpr auto reflect_call_scheme(Root*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"only",     "the parent's own">,
      ::reflect::param_scheme<"the_rest", "and the remainder">>{ };
  }

  constexpr auto reflect_interface_scheme(Root*)
  {
    using T = Root;
    return ::reflect::interface_scheme<
      ::reflect::method_scheme<"perform_action", &T::perform_action,
                               "do the thing">,
      ::reflect::method_scheme<"helper", &T::helper, "not a subcommand">>{ };
  }
}
