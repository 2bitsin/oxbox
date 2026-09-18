// The one file whose schemes come from buildutil reading the spec header;
// every other test in this module hand-writes them. So a failure here is
// as likely to be the generator, or the spec having moved.

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/hello-world-cli.hpp"
#include "oxbox/cli/unit.test/print-version-cli.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <list>
#include <string_view>
#include <string>
#include <vector>

namespace
{
  using oxbox::cli::CliStatus;
  using oxbox::cli::Command;
  using oxbox::cli::HelloWorld;
  using oxbox::cli::HelloWorldCli;
  using oxbox::cli::PrintVersion;
  using Args    = std::vector<std::string_view>;
  using Strings = std::vector<std::string>;

  namespace spec = oxbox::cli::detail::hello_world_cli;
  using Enumeration = spec::EnumerationVariable;

  constexpr auto SCHEME   { reflect::scheme_of<HelloWorld>()              };
  constexpr auto CALL     { reflect::call_scheme_of<HelloWorld>()         };
  constexpr auto APP      { reflect::scheme_of<HelloWorldCli>()           };
  constexpr auto INTERFACE{ reflect::interface_scheme_of<HelloWorldCli>() };
  constexpr auto VERSION  { reflect::scheme_of<PrintVersion>()            };

  // options and positionals in one line: they are read by two walks, and
  // the claim is that neither takes the other's tokens
  auto SpecLine() -> Args
  {
    return{
      "--default-negative-flag",                  // false by default: on
      "--default-positive-flag",                  // true  by default: off
      "--optional-string-value=i changed this",
      "--variable-string-array=a,b\\,c",          // the escape hides a comma
      "--variable-string-array=d",                // and occurrences add up
      "--variable-string-llist", "x,y",           // the space form, a list
      "--optional-int32t-value=-5",
      "--optional-uint64-value=42",
      "--optional-double-value=2.5",
      "--optional-optional-str=given",
      "--key-to-value-mappging:foo=hello",        // two occurrences of a
      "--key-to-value-mappging:bar=world,meh=blub",  // mapping, merged
      "--enumerated-option-val=option-tres",      // an enum, dashed name
      "7", "1.5", "2.5", "word", "3", "4.5", "and the rest" };
  }

  // ── conformance: what the generator read ─────────────────────────────

  static_assert(reflect::scheme_size(SCHEME) == 11u);
  static_assert(reflect::scheme_size(CALL) == 4u);
  static_assert(reflect::scheme_size(APP) == 4u);
  // three reflected methods: the subcommand factory, the inline subcommand
  // and Initialize, which the generator lists like any other public method
  static_assert(reflect::scheme_size(INTERFACE) == 3u);
  static_assert(reflect::scheme_size(VERSION) == 1u);

  // an operator() added to the application would make a bare `hello-world`
  // start running something instead of showing the screen that lists verbs
  static_assert(!reflect::interface_reflected<HelloWorld>);
  static_assert(!reflect::callable_reflected<HelloWorldCli>);
  static_assert(oxbox::cli::Initializes<HelloWorldCli>);
  static_assert(!oxbox::cli::HasInitialize<HelloWorld>);

  constexpr auto MEMBER0{ reflect::scheme_item<0>(SCHEME) };
  static_assert(MEMBER0.NAME_STRING == std::string_view{
    "default_negative_flag" });
  static_assert(MEMBER0.ENCAPSULATED == false);

  // the names come from the generator, the types from the language
  static_assert(reflect::call_traits_of<HelloWorld>::ARITY
                == reflect::scheme_size(CALL));

  // the last parameter is the tail, which makes the line's remainder legal
  static_assert(oxbox::cli::TailParameter<
    oxbox::cli::RangeView<std::string_view>>);
  static_assert(!oxbox::cli::TailParameter<std::array<float, 2>>);

  // a private member is reflected and is not an option
  constexpr auto HIDDEN{ reflect::scheme_item<0>(VERSION) };
  static_assert(HIDDEN.NAME_STRING == std::string_view{ "_version" });
  static_assert(HIDDEN.ENCAPSULATED == true);

  // ── the declarations the framework refuses ───────────────────────────
  // A refusal is a static_assert and cannot be run; what is pinned is the
  // condition each keys on -- positive on the spec, negative on a fixture.

  // the condition is reflected, and not "derives from Command"
  static_assert(oxbox::cli::HasMemberList<HelloWorld>);
  static_assert(oxbox::cli::HasMemberList<HelloWorldCli>);

  // the mistake itself: a subcommand target that never opted in
  struct Undeclared : Command
  {
    auto operator() () const -> oxbox::cli::CliResult { return{ }; }
  };
  static_assert(!oxbox::cli::HasMemberList<Undeclared>);

  // a layer declaring no operator() stays legal: a layer is not a verb
  static_assert( oxbox::cli::HasCallOperator<HelloWorld>);
  static_assert(!oxbox::cli::HasCallOperator<HelloWorldCli>);
  static_assert( oxbox::cli::ActionIsVisible<HelloWorld>);
  static_assert( oxbox::cli::ActionIsVisible<HelloWorldCli>);

  // members reflected and operator() not: only that pair is refused
  struct HalfDeclared : Command
  {
    friend constexpr auto reflect_scheme(HalfDeclared*);

    auto operator() (int code) const -> oxbox::cli::CliResult
    { return{ code }; }
  };

  constexpr auto reflect_scheme(HalfDeclared*)
  {
    return ::reflect::class_scheme<>{ };
  }

  static_assert( oxbox::cli::HasMemberList<HalfDeclared>);
  static_assert(!oxbox::cli::ActionIsVisible<HalfDeclared>);
  static_assert( oxbox::cli::ActionIsVisible<Undeclared>);

  TEST(TheGeneratedScheme, CarriesEveryMemberTheSpecDeclares)
  {
    EXPECT_EQ(reflect::scheme_size(SCHEME), 11u);

    // the leaf has no subcommand member to end the list with
    constexpr auto LAST{ reflect::scheme_item<10>(SCHEME) };
    EXPECT_EQ(LAST.NAME_STRING, "enumerated_option_val");
    EXPECT_FALSE(LAST.ENCAPSULATED);
  }

  TEST(TheGeneratedScheme, CarriesTheApplicationsOptionsAndBothSubcommands)
  {
    // the member spelling is in this list, the bare word in the interface
    EXPECT_EQ(reflect::scheme_size(APP), 4u);

    EXPECT_EQ(reflect::scheme_item<0>(APP).NAME_STRING, "greeting_language");
    EXPECT_EQ(reflect::scheme_item<2>(APP).NAME_STRING, "say_hello");
    EXPECT_EQ(reflect::scheme_item<3>(APP).NAME_STRING,
              "print_version_switch");

    // declaration order since buildutil 0.76.0: Initialize, declared last
    EXPECT_EQ(reflect::scheme_item<0>(INTERFACE).NAME_STRING,
              "print_version_positional");
    EXPECT_EQ(reflect::scheme_item<1>(INTERFACE).NAME_STRING, "say_greeting");
    EXPECT_EQ(reflect::scheme_item<2>(INTERFACE).NAME_STRING, "Initialize");
  }

  TEST(TheGeneratedScheme, CarriesTheCommentBesideTheMemberAsItsDescription)
  {
    constexpr auto STRING{ reflect::scheme_item<2>(SCHEME) };
    EXPECT_EQ(STRING.NAME_STRING, "optional_string_value");
    EXPECT_EQ(STRING.COMMENT,
              "This is an optional string with a default value.");
  }

  TEST(TheGeneratedScheme, KeepsAMultiLineCommentsOwnLineBreaks)
  {
    constexpr auto LIST{ reflect::scheme_item<4>(SCHEME) };
    EXPECT_EQ(LIST.NAME_STRING, "variable_string_llist");
    EXPECT_TRUE(std::string_view{ LIST.COMMENT }.starts_with(
      "Optional variable string list"));
    EXPECT_NE(std::string_view{ LIST.COMMENT }.find('\n'),
              std::string_view::npos);
  }

  TEST(TheGeneratedScheme, NamesEachPositionalParameterAndItsComment)
  {
    constexpr auto FIRST{ reflect::scheme_item<0>(CALL) };
    constexpr auto TAIL { reflect::scheme_item<3>(CALL) };

    EXPECT_EQ(FIRST.NAME_STRING, "value0");
    EXPECT_EQ(FIRST.COMMENT, "Description for value 0");
    EXPECT_EQ(TAIL.NAME_STRING, "value6toN");
    EXPECT_EQ(TAIL.COMMENT,
              "Description for the remaining parameters from 6 onward");
  }

  TEST(TheGeneratedScheme, ReadsTheEnumsEnumeratorsAndTheirComments)
  {
    constexpr auto VALUES{ reflect::scheme_of<Enumeration>() };
    constexpr auto SECOND{ reflect::scheme_item<1>(VALUES) };

    EXPECT_EQ(reflect::scheme_size(VALUES), 4u);
    EXPECT_EQ(SECOND.NAME_STRING, "OPTION_UNO");
    EXPECT_EQ(SECOND.COMMENT, "Description of OPTION_UNO");
    EXPECT_EQ(SECOND.VALUE, Enumeration::OPTION_UNO);
  }

  // ── the line: every kind of option the spec has, at once ─────────────

  TEST(TheSpecFixture, FillsEveryKindOfOptionFromOneLine)
  {
    HelloWorld hello;

    auto const result{ oxbox::cli::Apply(hello, SpecLine()) };
    ASSERT_EQ(result.Status(), CliStatus::RAN);

    // a flag takes the opposite of its default, either way round
    EXPECT_TRUE (hello.default_negative_flag);
    EXPECT_FALSE(hello.default_positive_flag);

    EXPECT_EQ(hello.optional_string_value, "i changed this");

    // the escaped comma is one value, and the second occurrence appends
    EXPECT_EQ(hello.variable_string_array, Strings({ "a", "b,c", "d" }));
    EXPECT_EQ(hello.variable_string_llist,
              std::list<std::string>({ "x", "y" }));

    EXPECT_EQ(hello.optional_int32t_value, -5);
    EXPECT_EQ(hello.optional_uint64_value, 42u);
    EXPECT_DOUBLE_EQ(hello.optional_double_value, 2.5);

    ASSERT_TRUE(hello.optional_optional_str.has_value());
    EXPECT_EQ(*hello.optional_optional_str, "given");

    EXPECT_EQ(hello.key_to_value_mappging,
              HelloWorld::S2SMapping({ { "foo", "hello" },
                                       { "bar", "world" },
                                       { "meh", "blub"  } }));

    EXPECT_EQ(hello.enumerated_option_val, Enumeration::OPTION_TRES);
  }

  TEST(TheSpecFixture, LeavesAnOptionNobodyMentionedAtItsDeclaredDefault)
  {
    HelloWorld hello;

    oxbox::cli::Apply(hello, Args{ "7", "1.5", "2.5", "word", "3", "4.5" });

    EXPECT_FALSE(hello.default_negative_flag);
    EXPECT_TRUE (hello.default_positive_flag);
    EXPECT_EQ(hello.optional_string_value, "i didn't change this");
    EXPECT_FALSE(hello.optional_optional_str.has_value());
    EXPECT_EQ(hello.enumerated_option_val, Enumeration::OPTION_UNO);
  }

  // ── the line: positionals reach operator() ───────────────────────────

  TEST(TheSpecFixture, RunsWhenItsSixDeclaredPositionalsAreThere)
  {
    HelloWorld hello;

    auto const result{ oxbox::cli::Apply(
      hello, Args{ "7", "1.5", "2.5", "word", "3", "4.5" }) };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 0);
    EXPECT_TRUE(result.Message().empty());
  }

  TEST(TheSpecFixture, GivesEverythingAfterTheSixthPositionalToTheTail)
  {
    HelloWorld hello;

    auto const result{ oxbox::cli::Apply(
      hello, Args{ "7", "1.5", "2.5", "word", "3", "4.5",
                   "one", "two", "three" }) };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
  }

  TEST(TheSpecFixture, NamesTheParameterThatRanOutOfPositionals)
  {
    HelloWorld hello;

    try {
      oxbox::cli::Apply(hello, Args{ "7", "1.5", "2.5", "word" });
      FAIL() << "expected a MissingArgument";
    } catch (oxbox::cli::MissingArgument const& failure) {
      // value0 and value1to2 were filled; the tuple is where it ran out
      EXPECT_EQ(failure.parameter, "value3to5");
      EXPECT_EQ(failure.description,
                "Description for combined value 3, 4 and 5");
    }
  }

  TEST(TheSpecFixture, ReportsABadTokenAgainstTheParameterItWasBoundTo)
  {
    // the second and third tokens are value1to2's, so a bad float is blamed
    // there
    HelloWorld hello;

    try {
      oxbox::cli::Apply(hello, Args{ "7", "1.5", "not-a-float",
                                     "word", "3", "4.5" });
      FAIL() << "expected a TypeMismatch";
    } catch (oxbox::utilities::TypeMismatch const& failure) {
      EXPECT_NE(std::string_view{ failure.what() }.find("value1to2"),
                std::string_view::npos);
    }
  }

  TEST(TheSpecFixture, RefusesAWordTheChildHasNowhereToPutBeforeAnythingRuns)
  {
    // PrintVersion takes no positionals, and pass one settles that over
    // types, before the application's own operator() has run
    HelloWorldCli app;

    try {
      oxbox::cli::Apply(app, Args{ "--print-version-switch", "extra" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "extra");
    }
  }

  // ── the line: one subcommand, two spellings ──────────────────────────

  TEST(TheSpecFixture, NamesOneAndTheSameSubcommandByEitherSpelling)
  {
    HelloWorldCli app;

    EXPECT_EQ(&app.print_version_switch,
              &Command::Get<PrintVersion>());
    EXPECT_EQ(&app.print_version_positional(),
              &Command::Get<PrintVersion>());
  }

  TEST(TheSpecFixture, FillsTheApplicationsOptionsBeforeItEntersASubcommand)
  {
    // the trigger ends the application's line, not its part in the run
    HelloWorldCli app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "--greeting-language=fr", "--print-version-switch" }) };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(app.greeting_language, "fr");
  }

  TEST(TheSpecFixture, RunsTheApplicationAndThenTheLeafItDispatchedTo)
  {
    HelloWorldCli app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "--verbose-run", "--say-hello",
                 "--optional-int32t-value=3",
                 "7", "1.5", "2.5", "word", "3", "4.5" }) };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 0);
    EXPECT_TRUE(app.verbose_run);
    EXPECT_EQ(Command::Get<HelloWorld>().optional_int32t_value, 3);
  }

  // ── the help screen, off the same schemes ────────────────────────────

  TEST(TheHelpScreen, ShowsTheShapeOfTheLineWhenItKnowsTheProgramName)
  {
    auto const screen{ oxbox::cli::FormatHelp<HelloWorld>("hello-world") };

    // value6toN was declared with a capital and is written back lowercased
    EXPECT_NE(screen.find("usage: hello-world [options] <value0> "
                          "<value1to2> <value3to5> [value6ton...]"),
              std::string_view::npos);
  }

  TEST(TheHelpScreen, ListsTheArgumentsAndTheOptionsOfTheLeafItIsAbout)
  {
    auto const screen{ oxbox::cli::FormatHelp<HelloWorld>("hello-world") };

    EXPECT_NE(screen.find("Arguments:"), std::string::npos);
    EXPECT_NE(screen.find("Options:"),   std::string::npos);

    EXPECT_NE(screen.find("value1to2"), std::string::npos);
    EXPECT_NE(screen.find("Description for combined"),
              std::string::npos);
    EXPECT_NE(screen.find("--optional-string-value"), std::string::npos);

    EXPECT_EQ(screen.find("Subcommands:"), std::string::npos);
  }

  TEST(TheHelpScreen, ListsTheApplicationsSubcommandsInBothSpellings)
  {
    auto const screen{ oxbox::cli::FormatHelp<HelloWorldCli>("hello-world") };

    EXPECT_NE(screen.find("Subcommands:"), std::string::npos);

    // the member says it with dashes, the method without
    EXPECT_NE(screen.find("--print-version-switch"), std::string::npos);
    EXPECT_NE(screen.find("print-version-positional"), std::string::npos);
    EXPECT_NE(screen.find("--say-hello"), std::string::npos);

    EXPECT_NE(screen.find("--greeting-language"), std::string::npos);
  }

  TEST(TheHelpScreen, DoesNotOfferInitializeAsThoughItWereASubcommand)
  {
    // a reflected method is a subcommand only if it returns a Command, and
    // Initialize returns a CliResult
    auto const screen{ oxbox::cli::FormatHelp<HelloWorldCli>("hello-world") };

    EXPECT_EQ(screen.find("initialize"), std::string::npos);
    EXPECT_EQ(screen.find("Initialize"), std::string::npos);
  }

  // ── the application is not a verb ────────────────────────────────────

  TEST(TheSpecApplication, ABareLineNamesNoActionAndGetsTheScreenInstead)
  {
    HelloWorldCli app;

    auto const result{ oxbox::cli::Apply(app, Args{ }, "hello-world") };

    EXPECT_EQ(result.Status(), CliStatus::NO_ACTION);
    EXPECT_EQ(result.Code(), oxbox::cli::USAGE_EXIT_CODE);
    EXPECT_TRUE(result.ShowsScreen());
    EXPECT_NE(result.Message().find("--say-hello"), std::string_view::npos);
    EXPECT_NE(result.Message().find("--greeting-language"),
              std::string_view::npos);
  }

  TEST(TheSpecApplication, TheSameLineWithHelpWrittenOnItExitsZero)
  {
    HelloWorldCli app;

    auto const fell {  oxbox::cli::Apply(app, Args{ }, "hello-world")         };
    auto const asked{ oxbox::cli::Apply(app, Args{ "--help" }, "hello-world") };

    EXPECT_EQ(fell.Message(), asked.Message());
    EXPECT_EQ(fell.Code(), 2);
    EXPECT_EQ(asked.Code(), 0);
    EXPECT_EQ(asked.Status(), CliStatus::HELP_SHOWN);
  }

  TEST(TheSpecApplication, AnArgvThatNamesNoVerbPrintsTheScreenAndExitsTwo)
  {
    // end to end, through the shape a real main() has
    char program[]{ "/usr/bin/hello-world" };
    char* argv[]{ program };

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main<HelloWorldCli>(1, argv) };
    std::fflush(stdout);
    std::fflush(stderr);
    auto const printed   { testing::internal::GetCapturedStdout() };
    auto const complained{ testing::internal::GetCapturedStderr() };

    EXPECT_EQ(result.Code(), 2);
    EXPECT_EQ(complained, "");
    EXPECT_NE(printed.find("usage: hello-world [options]"),
              std::string::npos);
    EXPECT_NE(printed.find("Subcommands:"), std::string::npos);
  }

  TEST(TheHelpScreen, ShowsAnEnumsAcceptedNamesAndTheDefaultItAlreadyHas)
  {
    auto const screen{ oxbox::cli::FormatHelp<HelloWorld>("hello-world") };

    EXPECT_NE(screen.find("one of: none, option-uno, option-dos, "
                          "option-tres"),
              std::string::npos);
    EXPECT_NE(screen.find("(default: option-uno)"), std::string::npos);
  }

  TEST(TheHelpScreen, IsWhatDashDashHelpAsksForAndNothingElseHappens)
  {
    HelloWorld hello;

    // no positionals at all, which would otherwise be a MissingArgument
    auto const result{ oxbox::cli::Apply(hello, Args{ "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_EQ(result.Code(), 0);
    EXPECT_NE(result.Message().find("--default-negative-flag"),
              std::string_view::npos);
    EXPECT_NE(result.Message().find("--help"), std::string_view::npos);
  }
}
