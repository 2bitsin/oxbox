// A command's bases are part of its command line.
// The fixtures are hand-written so the file is independent of the
// generator; derived_scheme and base_list are the shapes buildutil emits.

#include "oxbox/cli/help.hpp"
#include "oxbox/cli/main.hpp"
#include "oxbox/cli/parse.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>
#include <string>
#include <vector>

namespace oxbox::cli::unit_test
{
  // ── the shared options struct: not a Command, just a bundle ──────────
  // what makes a base contribute is that it carries a scheme
  struct CommonOptions
  {
    friend constexpr auto reflect_scheme(CommonOptions*);

    std::string        shared_thing{ "default" };  /* every verb needs this */
    bool               loud        { false };      /* say more while working */
    std::array<int, 2> span        { };            /* a fixed-extent pair */

  private:
    int _hidden{ 0 };                              /* nobody's option */
  };

  // a subcommand, so that a base can declare one
  struct Verb : Command
  {
    friend constexpr auto reflect_scheme(Verb*);
    friend constexpr auto reflect_call_scheme(Verb*);

    int level{ 0 };                      /* the subcommand's own option */

    auto operator() () const -> CliResult { return{ 9 }; }
  };

  struct WithSubcommand
  {
    friend constexpr auto reflect_scheme(WithSubcommand*);

    Verb inherited_verb{ };              /* a subcommand the base declares */
  };

  // the command under test: one option of its own, the rest inherited
  struct Inheritor : Command, CommonOptions, WithSubcommand
  {
    friend constexpr auto reflect_scheme(Inheritor*);
    friend constexpr auto reflect_call_scheme(Inheritor*);

    std::string own_thing{ "mine" };     /* only this command has this */

    auto operator() () const -> CliResult { return{ }; }
  };

  // ── two levels up, to prove the walk recurses ───────────────────────

  struct GrandBase
  {
    friend constexpr auto reflect_scheme(GrandBase*);

    int depth{ 0 };                      /* declared by the grandbase */
  };

  struct MiddleBase : GrandBase
  {
    friend constexpr auto reflect_scheme(MiddleBase*);

    std::string middle{ "m" };           /* declared by the middle base */
  };

  struct Deep : Command, MiddleBase
  {
    friend constexpr auto reflect_scheme(Deep*);
    friend constexpr auto reflect_call_scheme(Deep*);

    std::string own{ "o" };              /* declared by the command itself */

    auto operator() () const -> CliResult { return{ }; }
  };
}

// The shapes buildutil emits: captured bases make a derived_scheme whose
// first argument is the base_list; with none it stays a class_scheme.
namespace oxbox::cli::unit_test
{
  constexpr auto reflect_scheme(CommonOptions*)
  {
    using T = CommonOptions;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"shared_thing", &T::shared_thing,
                               "every verb needs this", false>,
      ::reflect::member_scheme<"loud", &T::loud,
                               "say more while working", false>,
      ::reflect::member_scheme<"span", &T::span,
                               "a fixed-extent pair", false>,
      ::reflect::member_scheme<"_hidden", &T::_hidden, "", true>>{ };
  }

  constexpr auto reflect_scheme(Verb*)
  {
    using T = Verb;
    return ::reflect::derived_scheme<
      ::reflect::base_list<::oxbox::cli::detail::command::Command>,
      ::reflect::member_scheme<"level", &T::level,
                               "the subcommand's own option", false>>{ };
  }

  constexpr auto reflect_call_scheme(Verb*)
  {
    return ::reflect::call_scheme<>{ };
  }

  constexpr auto reflect_scheme(WithSubcommand*)
  {
    using T = WithSubcommand;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"inherited_verb", &T::inherited_verb,
                               "a subcommand the base declares", false>>{ };
  }

  constexpr auto reflect_scheme(Inheritor*)
  {
    using T = Inheritor;
    return ::reflect::derived_scheme<
      ::reflect::base_list<::oxbox::cli::detail::command::Command,
                           CommonOptions, WithSubcommand>,
      ::reflect::member_scheme<"own_thing", &T::own_thing,
                               "only this command has this", false>>{ };
  }

  constexpr auto reflect_call_scheme(Inheritor*)
  {
    return ::reflect::call_scheme<>{ };
  }

  constexpr auto reflect_scheme(GrandBase*)
  {
    using T = GrandBase;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"depth", &T::depth,
                               "declared by the grandbase", false>>{ };
  }

  constexpr auto reflect_scheme(MiddleBase*)
  {
    using T = MiddleBase;
    return ::reflect::derived_scheme<
      ::reflect::base_list<GrandBase>,
      ::reflect::member_scheme<"middle", &T::middle,
                               "declared by the middle base", false>>{ };
  }

  constexpr auto reflect_scheme(Deep*)
  {
    using T = Deep;
    return ::reflect::derived_scheme<
      ::reflect::base_list<::oxbox::cli::detail::command::Command, MiddleBase>,
      ::reflect::member_scheme<"own", &T::own,
                               "declared by the command itself", false>>{ };
  }

  constexpr auto reflect_call_scheme(Deep*)
  {
    return ::reflect::call_scheme<>{ };
  }
}

namespace
{
  using oxbox::cli::CliStatus;
  using oxbox::cli::unit_test::Deep;
  using oxbox::cli::unit_test::Inheritor;
  using oxbox::cli::unit_test::Verb;
  using Args = std::vector<std::string_view>;

  auto Parsed(Args const& args) -> Inheritor
  {
    Inheritor command;
    oxbox::cli::ParseOptions(command, args);
    return command;
  }

  // ── the flattened list, as a compile-time count ──────────────────────
  // Command none, CommonOptions four (the private one is in the list and
  // refused later, by IsOption), WithSubcommand one, Inheritor one.
  static_assert(oxbox::cli::ItemCountOf<Inheritor> == 6u);

  // GrandBase one, MiddleBase one, Deep one; the Command tag base has none
  static_assert(oxbox::cli::ItemCountOf<Deep> == 3u);

  // ── options ──────────────────────────────────────────────────────────

  TEST(Inheritance, AnOptionDeclaredInABaseIsThisCommandsOptionToo)
  {
    auto const parsed{ Parsed({ "--shared-thing=from-the-base",
                                "--own-thing=from-the-command" }) };

    EXPECT_EQ(parsed.shared_thing, "from-the-base");
    EXPECT_EQ(parsed.own_thing, "from-the-command");
  }

  TEST(Inheritance, AnInheritedFlagTakesTheOppositeOfItsInheritedDefault)
  {
    EXPECT_TRUE(Parsed({ "--loud" }).loud);
    EXPECT_FALSE(Parsed({ }).loud);
  }

  TEST(Inheritance, AnInheritedOptionLeftUnsaidKeepsTheBasesDefault)
  {
    EXPECT_EQ(Parsed({ "--own-thing=x" }).shared_thing, "default");
  }

  TEST(Inheritance, TheWalkReachesAGrandbaseTheSameWayItReachesABase)
  {
    Deep deep;
    oxbox::cli::ParseOptions(deep, Args{ "--depth=3", "--middle=two",
                                         "--own=one" });

    EXPECT_EQ(deep.depth, 3);
    EXPECT_EQ(deep.middle, "two");
    EXPECT_EQ(deep.own, "one");
  }

  // ── arity: the seen[] bookkeeping is indexed by the flattened list ────

  TEST(Inheritance, AnInheritedScalarGivenTwiceIsRefusedLikeAnyOther)
  {
    try {
      Parsed({ "--shared-thing=a", "--shared-thing=b" });
      FAIL() << "expected a RepeatedOption";
    } catch (oxbox::cli::RepeatedOption const& failure) {
      EXPECT_EQ(failure.option, "shared-thing");
      EXPECT_EQ(failure.capacity, 1u);
    }
  }

  TEST(Inheritance, AnInheritedFixedExtentOptionCountsItsValuesNotItsMentions)
  {
    EXPECT_EQ(Parsed({ "--span=1", "--span=2" }).span,
              (std::array<int, 2>{ 1, 2 }));

    try {
      Parsed({ "--span=1,2,3" });
      FAIL() << "expected a RepeatedOption";
    } catch (oxbox::cli::RepeatedOption const& failure) {
      EXPECT_EQ(failure.option, "span");
      EXPECT_EQ(failure.capacity, 2u);
    }
  }

  TEST(Inheritance, EachInheritedOptionHasItsOwnAllowance)
  {
    auto const parsed{ Parsed({ "--shared-thing=a", "--own-thing=b",
                                "--span=1,2", "--loud" }) };

    EXPECT_EQ(parsed.shared_thing, "a");
    EXPECT_EQ(parsed.own_thing, "b");
    EXPECT_EQ(parsed.span, (std::array<int, 2>{ 1, 2 }));
    EXPECT_TRUE(parsed.loud);
  }

  // ── what inheritance does not bring across ───────────────────────────

  TEST(Inheritance, APrivateMemberOfABaseIsStillNobodysOption)
  {
    try {
      Parsed({ "--hidden=1" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.option, "hidden");
    }
  }

  TEST(Inheritance, ABaseWithNoSchemeContributesNothingAndSaysNothing)
  {
    // that it is passed over in silence is proved by this file running at all
    EXPECT_EQ(oxbox::cli::ItemCountOf<Verb>, 1u);
  }

  // ── suggestions ──────────────────────────────────────────────────────

  TEST(Inheritance, ANearMissOnABasesOptionIsSuggestedByName)
  {
    try {
      Parsed({ "--shared-thig" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.suggestion, "shared-thing");
    }
  }

  TEST(Inheritance, TheSuggestionVocabularyHoldsBothLevelsAtOnce)
  {
    auto const declared{ oxbox::cli::OptionNames<Inheritor>() };

    auto const has = [&declared](std::string_view name) {
      return std::ranges::find(declared, name) != declared.end();
    };

    EXPECT_TRUE(has("shared_thing"));
    EXPECT_TRUE(has("own_thing"));
    EXPECT_FALSE(has("_hidden"));
  }

  // ── subcommands ──────────────────────────────────────────────────────

  TEST(Inheritance, ASubcommandDeclaredInABaseDispatches)
  {
    Inheritor command;

    auto const result{ oxbox::cli::Apply(command, Args{ "--inherited-verb" }) };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 9);
  }

  TEST(Inheritance, ThePartOfTheLineBeforeAnInheritedTriggerStillLands)
  {
    Inheritor command;

    oxbox::cli::Apply(command, Args{ "--shared-thing=set", "--inherited-verb",
                                     "--level=4" });

    EXPECT_EQ(command.shared_thing, "set");
    EXPECT_EQ(command.inherited_verb.level, 4);
  }

  TEST(Inheritance, AnInheritedSubcommandIsNotAnOptionAndTakesNoValue)
  {
    try {
      Parsed({ "--inherited-verb=1" });
      FAIL() << "expected a SubcommandTakesNoValue";
    } catch (oxbox::cli::SubcommandTakesNoValue const& failure) {
      EXPECT_EQ(failure.option, "inherited-verb");
    }
  }

  // ── help ─────────────────────────────────────────────────────────────

  TEST(Inheritance, TheHelpScreenListsWhatTheBaseDeclared)
  {
    auto const screen{ oxbox::cli::FormatHelp<Inheritor>() };

    EXPECT_NE(screen.find("--shared-thing"), std::string::npos);
    EXPECT_NE(screen.find("every verb needs this"), std::string::npos);
    EXPECT_NE(screen.find("--own-thing"), std::string::npos);
    EXPECT_NE(screen.find("--inherited-verb"), std::string::npos);
    EXPECT_EQ(screen.find("--hidden"), std::string::npos);
  }

  TEST(Inheritance, AnInheritedOptionShowsTheDefaultTheBaseGaveIt)
  {
    auto const screen{ oxbox::cli::FormatHelp<Inheritor>() };

    EXPECT_NE(screen.find("(default: \"default\")"), std::string::npos);
  }

  TEST(Inheritance, TheHelpScreenListsBasesFirstAndInDeclarationOrder)
  {
    // the same order serialization concatenates in
    auto const screen{ oxbox::cli::FormatHelp<Deep>() };

    auto const grand { screen.find("--depth")  };
    auto const middle{ screen.find("--middle") };
    auto const own   { screen.find("--own ")   };

    ASSERT_NE(grand, std::string::npos);
    ASSERT_NE(middle, std::string::npos);
    ASSERT_NE(own, std::string::npos);
    EXPECT_LT(grand, middle);
    EXPECT_LT(middle, own);
  }

  // ── --help itself ────────────────────────────────────────────────────

  TEST(Inheritance, TheFrameworkStillBindsHelpWhenNoLevelClaimedIt)
  {
    Inheritor command;

    auto const result{ oxbox::cli::Apply(command, Args{ "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
  }
}
