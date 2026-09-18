#pragma once

// The fixtures for labels and the rest collector, declared the way a real
// command is, so the tests exercise the generator's own label plumbing
// and not a hand-written scheme that agrees with it by construction.

#include "oxbox/cli/main.hpp"

#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace oxbox::cli::detail::rest_cli
{
  struct Paragraphs : Command
  {
    friend constexpr auto reflect_scheme(Paragraphs*);

    bool folded{ false }; /* This description starts on one source line
                             and continues on another source line
                             before ending on a third source line. */
    bool separate{ false }; /* The first paragraph spans
                               two source lines.

                               The second paragraph also spans
                               two source lines. */
  };

  struct Undocumented : Command
  {
    friend constexpr auto reflect_scheme(Undocumented*);

    auto foo(std::string x) -> CliResult;
  };

  // a wrapper: its own two options, and a tail belonging to what it launches
  struct Wrapper : Command
  {
    friend constexpr auto reflect_scheme(Wrapper*);

    bool headless{ false };                       /* windowless rendering */
    // the label replaces the name: this is --secure, and --tls is unknown
    _Label(secure) bool tls{ false };             /* require TLS */

    // the collector: everything after a bare `--`, verbatim and in order
    _Label(--)
    std::vector<std::string> chromium_switches{ };  /* handed over untouched */

    auto operator() (std::optional<std::string> url /* the page to open */)
      -> CliResult;
  };

  // the same claim, borrowed: the views point into Main's argument storage
  struct Borrowing : Command
  {
    friend constexpr auto reflect_scheme(Borrowing*);

    bool quiet{ false };                            /* say less */
    _Label(--)
    std::vector<std::string_view> passthrough{ };   /* viewed, never copied */

    auto operator() () -> CliResult;
  };

  // the lossy one: a token containing a space is indistinguishable from two
  struct Joining : Command
  {
    friend constexpr auto reflect_scheme(Joining*);

    _Label(--)
    std::string command_line{ };                    /* the tail as one line */

    auto operator() () -> CliResult;
  };

  // Two names that differ only after the transform, so the collision check
  // has to run on a pair it could have merged. That refusal is a
  // static_assert, and a static_assert that fires is a build that stopped.
  struct NearMiss : Command
  {
    friend constexpr auto reflect_scheme(NearMiss*);

    std::string content_type{ };                    /* the media type */
    _Label(content_types)
    std::string kinds{ };                           /* the ones accepted */

    auto operator() () -> CliResult;
  };

  // the contrast: no collector, so the tail is positional as it always was
  struct Plain : Command
  {
    friend constexpr auto reflect_scheme(Plain*);

    bool headless{ false };                         /* windowless rendering */

    auto operator() (std::vector<std::string> words /* whatever is left */)
      -> CliResult;
  };

  // a collector on a dispatching layer: whose segment the sentinel is in
  struct Leaf : Command
  {
    friend constexpr auto reflect_scheme(Leaf*);

    int depth{ 0 };                                 /* how deep */
    _Label(--)
    std::vector<std::string> leaf_rest{ };          /* the leaf's own tail */

    auto operator() () -> CliResult;
  };

  struct Layered : Command
  {
    friend constexpr auto reflect_scheme(Layered*);

    bool verbose{ false };                          /* say more on the way */
    Leaf& leaf{ Command::Get<Leaf>() };             /* descend into the leaf */
    _Label(--)
    std::vector<std::string> parent_rest{ };        /* the parent's own tail */

    auto operator() () -> CliResult;
  };

  // The markers sit in the two positions the attribute they stand for is
  // legal in: after an enumerator's name, before a parameter's type.
  enum class Tint : int
  {
    DEEP_BLUE _Label(ocean) = 1,                  /* the cool one */
    PALE_RED  _Label(coral),                      /* the warm one */
  };

  constexpr auto reflect_scheme(Tint*);

  struct Painter : Command
  {
    friend constexpr auto reflect_scheme(Painter*);

    Tint tint{ Tint::DEEP_BLUE };                 /* which shade to paint in */

    auto operator() (_Label(page) std::string url /* the page to paint */)
      -> CliResult;
  };

  // a label renames a subcommand member and both kinds of reflected method
  struct Renamed : Command
  {
    friend constexpr auto reflect_scheme(Renamed*);

    _Label(kid) Leaf& child{ Command::Get<Leaf>() }; /* reached as --kid */

    _Label(list-them) auto enumerate() -> Leaf&;     /* reached as list-them */

    _Label(say-hi) auto greet(std::string who /* who is being greeted */)
      -> CliResult;                                 /* reached as say-hi */
  };
}

namespace oxbox::cli
{
  using detail::rest_cli::Borrowing;
  using detail::rest_cli::Joining;
  using detail::rest_cli::Layered;
  using detail::rest_cli::Leaf;
  using detail::rest_cli::NearMiss;
  using detail::rest_cli::Painter;
  using detail::rest_cli::Plain;
  using detail::rest_cli::Renamed;
  using detail::rest_cli::Tint;
  using detail::rest_cli::Wrapper;
}
