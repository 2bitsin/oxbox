// The option parser, against a fixture that exercises each rule the type
// system is supposed to decide: a flag defaulting false and one
// defaulting true, a member whose NAME carries the negation, a private
// member, an optional, an enum, a nested command, a dynamic container, a
// fixed-extent one, and a mapping.

#include "oxbox/cli/main.hpp"
#include "oxbox/cli/parse.hpp"
#include "oxbox/cli/unit.test/rest-cli.hpp"

#include <gtest/gtest.h>

#include <array>
#include <list>
#include <optional>
#include <set>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace oxbox::cli::unit_test
{
  enum class Tint : int { DEEP_BLUE = 1, PALE_RED };
  constexpr auto reflect_scheme(Tint*);

  struct Nested : Command
  {
    friend constexpr auto reflect_scheme(Nested*);
    int depth { 0 };
  };

  struct Options : Command
  {
    friend constexpr auto reflect_scheme(Options*);

    bool               verbose { false };            /* say more */
    bool               colour  { true  };            /* on unless asked */
    bool               no_banner { false };          /* suppress the banner */
    int                retries { 3 };                /* how many times to try */
    std::string        name    { "x" };              /* who to greet */
    std::optional<int> limit   { };                  /* absent unless given */
    Tint               tint    { Tint::DEEP_BLUE };  /* which shade */
    Nested             nested  { };                  /* a subcommand */
    int                quiet   { 0 };                // no comment shown below

    std::vector<std::string> tags        { };        /* as many as you like */
    std::array<int, 2>       pair_values { };        /* exactly two */
    std::unordered_map<std::string, std::string>
                             mapping     { };        /* key to value */

  private:
    int _hidden { 7 };                               /* never an option */
  };

  // A second fixture for the other ways a container accepts a value: a
  // list has emplace_back, a set has only insert. Nothing about the
  // parser distinguishes them, and that is the claim under test.
  struct Baskets : Command
  {
    friend constexpr auto reflect_scheme(Baskets*);

    std::list<std::string> lines   { };              /* appended to */
    std::set<int>          numbers { };              /* inserted into */
  };
}

// Hand-written to keep this test independent of the generator; the shapes
// match what buildutil emits (see the generated header for hello-world).
namespace oxbox::cli::unit_test
{
  constexpr auto reflect_scheme(Tint*)
  {
    return ::reflect::enum_scheme<
      ::reflect::enumerator<"DEEP_BLUE", Tint::DEEP_BLUE, "the cool one">,
      ::reflect::enumerator<"PALE_RED",  Tint::PALE_RED,  "the warm one">>{ };
  }

  constexpr auto reflect_scheme(Nested*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"depth", &Nested::depth, "how deep", false>>{ };
  }

  constexpr auto reflect_scheme(Baskets*)
  {
    using T = Baskets;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"lines",   &T::lines,   "appended to",   false>,
      ::reflect::member_scheme<"numbers", &T::numbers, "inserted into", false>>{ };
  }

  constexpr auto reflect_scheme(Options*)
  {
    using T = Options;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"verbose",   &T::verbose,   "say more",             false>,
      ::reflect::member_scheme<"colour",    &T::colour,    "on unless asked",      false>,
      ::reflect::member_scheme<"no_banner", &T::no_banner, "suppress the banner",  false>,
      ::reflect::member_scheme<"retries",   &T::retries,   "how many times to try",false>,
      ::reflect::member_scheme<"name",      &T::name,      "who to greet",         false>,
      ::reflect::member_scheme<"limit",     &T::limit,     "absent unless given",  false>,
      ::reflect::member_scheme<"tint",      &T::tint,      "which shade",          false>,
      ::reflect::member_scheme<"nested",    &T::nested,    "a subcommand",         false>,
      ::reflect::member_scheme<"quiet",     &T::quiet,     "",                     false>,
      ::reflect::member_scheme<"tags",      &T::tags,      "as many as you like",  false>,
      ::reflect::member_scheme<"pair_values",&T::pair_values,"exactly two",         false>,
      ::reflect::member_scheme<"mapping",   &T::mapping,   "key to value",         false>,
      ::reflect::member_scheme<"_hidden",   &T::_hidden,   "never an option",      true >>{ };
  }
}

namespace
{
  using oxbox::cli::unit_test::Baskets;
  using oxbox::cli::unit_test::Options;
  using oxbox::cli::unit_test::Tint;
  using Args    = std::vector<std::string_view>;
  using Strings = std::vector<std::string>;
  using Mapping = std::unordered_map<std::string, std::string>;

  auto Parsed(Args const& args) -> Options
  {
    Options options;
    oxbox::cli::ParseOptions(options, args);
    return options;
  }

  auto ParsedBaskets(Args const& args) -> Baskets
  {
    Baskets baskets;
    oxbox::cli::ParseOptions(baskets, args);
    return baskets;
  }

  // ── flags: presence takes the opposite of the default ────────────────

  TEST(Flags, PresenceTurnsAFalseDefaultOn)
  {
    EXPECT_TRUE(Parsed({ "--verbose" }).verbose);
  }

  TEST(Flags, PresenceTurnsATrueDefaultOff)
  {
    // the reason there is no --no- prefix to invent: a flag is a toggle
    // relative to whatever the member was declared as
    EXPECT_FALSE(Parsed({ "--colour" }).colour);
  }

  TEST(Flags, TheNegationLivesInTheMemberNameNotTheFramework)
  {
    EXPECT_TRUE(Parsed({ "--no-banner" }).no_banner);
  }

  TEST(Flags, RepeatingAFlagIsRefusedLikeAnyOtherScalar)
  {
    // A flag is not a toggle that could be flipped twice back to where it
    // started -- it is a scalar, and the arity rule is the arity rule: a
    // scalar takes one occurrence, and a second is a mistake worth
    // naming rather than an instruction to undo the first.
    EXPECT_THROW(Parsed({ "--verbose", "--verbose" }),
                 oxbox::cli::RepeatedOption);
  }

  TEST(Flags, AnExplicitValueStillWins)
  {
    EXPECT_FALSE(Parsed({ "--verbose=false" }).verbose);
  }

  TEST(Flags, AFlagNeverConsumesTheNextArgument)
  {
    auto const options{ Parsed({ "--verbose", "--name", "ada" }) };
    EXPECT_TRUE(options.verbose);
    EXPECT_EQ(options.name, "ada");
  }

  // ── values ───────────────────────────────────────────────────────────

  TEST(Values, AttachedWithAnEqualsSign)
  {
    EXPECT_EQ(Parsed({ "--retries=9" }).retries, 9);
  }

  TEST(Values, AttachedWithASpace)
  {
    EXPECT_EQ(Parsed({ "--retries", "9" }).retries, 9);
  }

  TEST(Values, AnOptionalIsAbsentUntilGiven)
  {
    EXPECT_FALSE(Parsed({ }).limit.has_value());
    EXPECT_EQ(Parsed({ "--limit=5" }).limit.value(), 5);
  }

  TEST(Values, AnEnumResolvesThroughItsReflectedNames)
  {
    EXPECT_EQ(Parsed({ "--tint=pale-red" }).tint, Tint::PALE_RED);
  }

  TEST(Values, TrailingGarbageIsRefused)
  {
    EXPECT_THROW(Parsed({ "--retries=12abc" }),
                 oxbox::utilities::TypeMismatch);
  }

  TEST(Values, AnUnknownEnumeratorSaysWhatIsAccepted)
  {
    try {
      Parsed({ "--tint=chartreuse" });
      FAIL() << "expected a TypeMismatch";
    } catch (oxbox::utilities::TypeMismatch const& failure) {
      EXPECT_NE(std::string_view{ failure.what() }.find("deep-blue"),
                std::string_view::npos);
    }
  }

  TEST(Values, MismatchesIncludeTheTypedValue)
  {
    auto check = []<typename Type>(std::string_view text,
                                  std::string_view option,
                                  std::string_view expected) {
      try {
        oxbox::cli::FromText<Type>(text, option);
        FAIL() << "expected a TypeMismatch";
      } catch (oxbox::utilities::TypeMismatch const& failure) {
        EXPECT_EQ(std::string_view{ failure.what() }, expected);
      }
    };
    check.template operator()<oxbox::cli::detail::rest_cli::Tint>(
      "deep-blue", "tint",
      "type mismatch at tint: expected one of: ocean, coral, got 'deep-blue'");
    check.template operator()<bool>("maybe", "verbose",
      "type mismatch at verbose: expected true or false, got 'maybe'");
    check.template operator()<int>("12abc", "retries",
      "type mismatch at retries: expected an integer, got '12abc'");
    check.template operator()<double>("1.2x", "ratio",
      "type mismatch at ratio: expected a number, got '1.2x'");
  }

  // ── what is and is not an option ─────────────────────────────────────

  TEST(Options, APrivateMemberIsNotAnOption)
  {
    // reflection CAN see it -- that is what the friend tag grants -- and
    // the parser is where it declines to expose it
    EXPECT_THROW(Parsed({ "--hidden=1" }), oxbox::cli::UnknownOption);
  }

  TEST(Options, ANestedCommandIsNotAnOptionButASubcommand)
  {
    // it is declared, so this is not an unknown name; it is a subcommand,
    // and a subcommand has no value to be given -- the trigger ends this
    // command's line and everything after it belongs to the child
    try {
      Parsed({ "--nested=1" });
      FAIL() << "expected a SubcommandTakesNoValue";
    } catch (oxbox::cli::SubcommandTakesNoValue const& failure) {
      EXPECT_EQ(failure.option, "nested");
    }
  }

  // ── failures name what went wrong ────────────────────────────────────

  TEST(Failures, AnUnknownOptionSuggestsItsNearestNeighbour)
  {
    try {
      Parsed({ "--verbos" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.option, "verbos");
      EXPECT_EQ(failure.suggestion, "verbose");
    }
  }

  // A suggestion is something to retype, so it is the command-line
  // spelling of the name and never the declared one -- an underscore in
  // the member is the case that tells the two apart.
  TEST(Failures, ASuggestionIsSpelledTheWayItMustBeTyped)
  {
    try {
      Parsed({ "--pair-value" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.suggestion, "pair-values");
      EXPECT_TRUE(std::string_view{ failure.what() }.find("--pair-values")
                  != std::string_view::npos);
    }
  }

  TEST(Failures, NothingCloseEnoughGetsNoSuggestion)
  {
    try {
      Parsed({ "--zzzzzzzz" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_TRUE(failure.suggestion.empty());
    }
  }

  TEST(Failures, AScalarGivenTwiceIsRefused)
  {
    try {
      Parsed({ "--retries=1", "--retries=2" });
      FAIL() << "expected a RepeatedOption";
    } catch (oxbox::cli::RepeatedOption const& failure) {
      EXPECT_EQ(failure.option, "retries");
      EXPECT_EQ(failure.capacity, 1u);
    }
  }

  TEST(Failures, AValueOptionAtTheEndOfTheLineHasNoValue)
  {
    try {
      Parsed({ "--name" });
      FAIL() << "expected a MissingValue";
    } catch (oxbox::cli::MissingValue const& failure) {
      EXPECT_EQ(failure.option, "name");
    }
  }

  // ── lists: one option, several values ────────────────────────────────

  TEST(Lists, ACommaSeparatedValueBecomesSeveralValues)
  {
    EXPECT_EQ(Parsed({ "--tags=a,b,c" }).tags, Strings({ "a", "b", "c" }));
  }

  TEST(Lists, TheSpaceFormSplitsTheSameWayTheAttachedFormDoes)
  {
    // where the value came from is not a property of the value
    EXPECT_EQ(Parsed({ "--tags", "a,b" }).tags, Strings({ "a", "b" }));
  }

  TEST(Lists, RepeatedOccurrencesAccumulateRatherThanReplace)
  {
    EXPECT_EQ(Parsed({ "--tags=a", "--tags=b,c" }).tags,
              Strings({ "a", "b", "c" }));
  }

  TEST(Lists, AnEscapedCommaIsPartOfTheValue)
  {
    EXPECT_EQ(Parsed({ "--tags=a\\,b" }).tags, Strings({ "a,b" }));
  }

  TEST(Lists, AnEscapedBackslashIsABackslashAndDoesNotEscapeWhatFollows)
  {
    EXPECT_EQ(Parsed({ "--tags=a\\\\,b" }).tags, Strings({ "a\\", "b" }));
  }

  TEST(Lists, ATrailingBackslashHasNothingToEscapeAndIsItself)
  {
    EXPECT_EQ(Parsed({ "--tags=a\\" }).tags, Strings({ "a\\" }));
  }

  TEST(Lists, AListNeverFillsUp)
  {
    EXPECT_EQ(Parsed({ "--tags=a", "--tags=b", "--tags=c,d,e" }).tags.size(),
              5u);
  }

  TEST(Lists, AListIsFilledTheSameWayAVectorIs)
  {
    auto const baskets{ ParsedBaskets({ "--lines=a,b", "--lines=c" }) };
    EXPECT_EQ(Strings(baskets.lines.begin(), baskets.lines.end()),
              Strings({ "a", "b", "c" }));
  }

  TEST(Lists, AContainerThatOnlyKnowsInsertIsFilledTheSameWayToo)
  {
    // how a container accepts a value is the container's business; the
    // parser asks for whichever of the four spellings it offers
    EXPECT_EQ(ParsedBaskets({ "--numbers=3,1", "--numbers=2" }).numbers,
              std::set<int>({ 1, 2, 3 }));
  }

  // ── a fixed-extent member counts values, not occurrences ─────────────

  TEST(Arrays, TwoValuesInOneOccurrenceFillAPair)
  {
    EXPECT_EQ(Parsed({ "--pair-values=4,5" }).pair_values,
              (std::array<int, 2>{ 4, 5 }));
  }

  TEST(Arrays, TwoValuesAcrossTwoOccurrencesFillItJustTheSame)
  {
    EXPECT_EQ(Parsed({ "--pair-values=4", "--pair-values=5" }).pair_values,
              (std::array<int, 2>{ 4, 5 }));
  }

  TEST(Arrays, AThirdValueHasNowhereToGoAndIsRefused)
  {
    try {
      Parsed({ "--pair-values=4,5", "--pair-values=6" });
      FAIL() << "expected a RepeatedOption";
    } catch (oxbox::cli::RepeatedOption const& failure) {
      EXPECT_EQ(failure.option, "pair-values");
      EXPECT_EQ(failure.capacity, 2u);
      // the refusal speaks of values, because that is what was counted
      EXPECT_NE(std::string_view{ failure.what() }.find("at most 2 values"),
                std::string_view::npos);
    }
  }

  TEST(Arrays, AThirdValueInASingleOccurrenceIsRefusedToo)
  {
    EXPECT_THROW(Parsed({ "--pair-values=4,5,6" }),
                 oxbox::cli::RepeatedOption);
  }

  TEST(Arrays, EachElementIsConvertedAndAnElementThatIsNotAnIntegerIsRefused)
  {
    EXPECT_THROW(Parsed({ "--pair-values=1,x" }),
                 oxbox::utilities::TypeMismatch);
  }

  // ── mappings: the colon is what says entries follow ──────────────────

  TEST(Mappings, AColonIntroducesOneKeyEqualsValueEntry)
  {
    EXPECT_EQ(Parsed({ "--mapping:foo=hello" }).mapping,
              Mapping({ { "foo", "hello" } }));
  }

  TEST(Mappings, SeveralEntriesAreCommaSeparatedAndOccurrencesMerge)
  {
    EXPECT_EQ(Parsed({ "--mapping:foo=hello",
                       "--mapping:bar=world,meh=blub" }).mapping,
              Mapping({ { "foo", "hello" },
                        { "bar", "world" },
                        { "meh", "blub" } }));
  }

  TEST(Mappings, ARepeatedKeyKeepsTheLastValueGiven)
  {
    // a second mention of a key reads as a correction; there is nowhere
    // in a mapping for two values under one key to both live
    EXPECT_EQ(Parsed({ "--mapping:k=first", "--mapping:k=second" }).mapping,
              Mapping({ { "k", "second" } }));
  }

  TEST(Mappings, AnEscapedSeparatorStaysInsideTheKeyOrTheValue)
  {
    EXPECT_EQ(Parsed({ "--mapping:a\\=b=c\\,d" }).mapping,
              Mapping({ { "a=b", "c,d" } }));
  }

  TEST(Mappings, AMappingGivenAPlainValueIsRefusedAndNamesTheSyntax)
  {
    try {
      Parsed({ "--mapping=foo" });
      FAIL() << "expected a MalformedMapping";
    } catch (oxbox::cli::MalformedMapping const& failure) {
      EXPECT_EQ(failure.option, "mapping");
      EXPECT_NE(std::string_view{ failure.what() }.find("--name:key=value"),
                std::string_view::npos);
    }
  }

  TEST(Mappings, TheSpaceFormIsNotAMappingEither)
  {
    EXPECT_THROW(Parsed({ "--mapping", "foo=bar" }),
                 oxbox::cli::MalformedMapping);
  }

  TEST(Mappings, AnEntryWithNoEqualsSignHasNoKeyToMergeUnder)
  {
    try {
      Parsed({ "--mapping:foo=hello,bar" });
      FAIL() << "expected a MalformedMapping";
    } catch (oxbox::cli::MalformedMapping const& failure) {
      EXPECT_EQ(failure.option, "mapping");
      EXPECT_NE(std::string_view{ failure.what() }.find("'bar'"),
                std::string_view::npos);
    }
  }

  TEST(Mappings, TheEntrySyntaxIsRefusedForAnOptionThatIsNotAMapping)
  {
    EXPECT_THROW(Parsed({ "--name:foo=bar" }),
                 oxbox::cli::MalformedMapping);
  }

  TEST(Mappings, AnUnknownOptionIsStillUnknownInTheEntryForm)
  {
    // the name is what precedes the colon, so the report is about the
    // option and not about some option called "nosuch:k"
    try {
      Parsed({ "--nosuchthing:k=v" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.option, "nosuchthing");
    }
  }

  TEST(Mappings, AScalarStringKeepsItsCommasBecauseItIsNotAList)
  {
    EXPECT_EQ(Parsed({ "--name=a,b,c" }).name, "a,b,c");
  }

  // ── arity comes from the type ────────────────────────────────────────

  static_assert(oxbox::cli::ArityOf<int>()                 == 1u);
  static_assert(oxbox::cli::ArityOf<std::string>()         == 1u);
  static_assert(oxbox::cli::ArityOf<std::optional<int>>()  == 1u);
  static_assert(oxbox::cli::ArityOf<std::array<float, 2>>() == 2u);
  static_assert(oxbox::cli::ArityOf<std::vector<int>>()
                == oxbox::cli::UNBOUNDED);
  static_assert(oxbox::cli::ArityOf<Mapping>() == oxbox::cli::UNBOUNDED);
}
