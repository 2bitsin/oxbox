// The inline subcommand: a reflected method answering a CliResult, and
// therefore the leaf rather than the way to one. The fixtures record into
// one thread-local Trace because most of what follows is about order.

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/hello-world-cli.hpp"
#include "oxbox/cli/unit.test/inline-subcommand-cli.inc"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <optional>
#include <string_view>
#include <string>
#include <tuple>
#include <vector>

namespace
{
  using oxbox::cli::CliStatus;
  using oxbox::cli::MethodRole;
  using oxbox::cli::inline_test::App;
  using oxbox::cli::inline_test::Mid;
  using oxbox::cli::inline_test::Seen;
  using oxbox::cli::inline_test::Trace;
  using Args    = std::vector<std::string_view>;
  using Strings = std::vector<std::string>;

  auto Fresh() -> void { Seen() = Trace{ }; }

  // ── which methods are subcommands, as a compile-time claim ───────────
  // the interface is in the generator's order, sorted by name

  constexpr auto INTERFACE{ ::reflect::interface_scheme_of<App>() };

  template <std::size_t INDEX>
  using Method = decltype(::reflect::scheme_item<INDEX>(INTERFACE));

  static_assert(Method<0>::NAME_STRING == std::string_view{ "Initialize" });
  static_assert(Method<5>::NAME_STRING == std::string_view{ "status" });

  // Initialize fits the shape of a verb exactly and is excluded by name
  static_assert(oxbox::cli::RoleOf<Method<0>>() == MethodRole::NONE);

  // the rule is exact and not convertible-to: an int is not a CliResult
  static_assert(oxbox::cli::RoleOf<Method<5>>() == MethodRole::NONE);

  // the three that are inline subcommands, and the one that descends
  static_assert(oxbox::cli::RoleOf<Method<1>>() == MethodRole::ACTS);
  static_assert(oxbox::cli::RoleOf<Method<2>>() == MethodRole::ACTS);
  static_assert(oxbox::cli::RoleOf<Method<3>>() == MethodRole::ACTS);
  static_assert(oxbox::cli::RoleOf<Method<4>>() == MethodRole::DESCENDS);

  // the same predicate an operator()'s parameters are held to
  using MaybeParameters =
    typename oxbox::cli::MethodSignature<Method<3>>::Parameters;

  static_assert(oxbox::cli::OptionalsComeLast<MaybeParameters>());
  static_assert(!oxbox::cli::OptionalsComeLast<
    std::tuple<std::optional<int>, std::string>>());

  // the parameter types come from the language, not from the scheme's names
  static_assert(std::tuple_size_v<MaybeParameters> == 2u);
  static_assert(oxbox::cli::MinIntake<
    std::tuple_element_t<1, MaybeParameters>>() == 0u);

  // ── the trigger ──────────────────────────────────────────────────────

  TEST(InlineSubcommands, ABareWordDispatchesToAMethodThatAnswersACliResult)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(app, Args{ "chat", "hello" }) };

    EXPECT_EQ(Seen().steps, Strings({ "app", "chat:hello" }));
    EXPECT_EQ(result.Code(), 11);
    EXPECT_EQ(result.Status(), CliStatus::RAN);
  }

  TEST(InlineSubcommands, TheParentsOptionsAreInAndItHasComeUpBeforeTheLeafRuns)
  {
    Fresh();
    App app;

    oxbox::cli::Apply(app, Args{ "--label=given", "chat", "hi" });

    EXPECT_EQ(Seen().steps, Strings({ "app", "chat:hi" }));
    EXPECT_EQ(Seen().label, "given");
    EXPECT_EQ(app.label, "given");
  }

  TEST(InlineSubcommands, TheSameWordAfterAPositionalIsJustAPositional)
  {
    // the first-slot rule is the existing one; an inline name gets no second
    Fresh();
    App app;

    try {
      oxbox::cli::Apply(app, Args{ "first", "chat" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "first");
    }
    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineSubcommands, TheSameWordAfterTheSentinelIsJustAPositionalToo)
  {
    Fresh();
    App app;

    try {
      oxbox::cli::Apply(app, Args{ "--", "chat" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "chat");
    }
    EXPECT_TRUE(Seen().steps.empty());
  }

  // ── the arguments: invoke.hpp's rules, unchanged ─────────────────────

  TEST(InlineSubcommands, EachParameterShapeTakesWhatItsTypeIsWorth)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "blend", "2", "3", "word", "4", "x", "y" }) };

    EXPECT_EQ(result.Code(), 13);
    EXPECT_EQ(Seen().pair, (std::array<int, 2>{ 2, 3 }));  // array: two
    EXPECT_EQ(Seen().word, "word");                        // tuple: two more,
    EXPECT_EQ(Seen().number, 4);                           // element by element
    EXPECT_EQ(Seen().rest, Strings({ "x", "y" }));         // the tail: the rest
  }

  TEST(InlineSubcommands, TheTailIsEmptyWhenTheOthersTookEverything)
  {
    Fresh();
    App app;

    oxbox::cli::Apply(app, Args{ "blend", "2", "3", "word", "4" });

    EXPECT_TRUE(Seen().rest.empty());
  }

  TEST(InlineSubcommands, AnOptionalParameterTakesTheWordWhenThereIsOne)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(app, Args{ "maybe", "now", "5" }) };

    EXPECT_EQ(result.Code(), 17);
    EXPECT_EQ(Seen().steps, Strings({ "app", "maybe:now" }));
    EXPECT_EQ(Seen().number, 5);
  }

  TEST(InlineSubcommands, AnOptionalParameterLeftOutIsTheAnswerAndNotAFailure)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(app, Args{ "maybe", "now" }) };

    EXPECT_EQ(result.Code(), 17);
    EXPECT_EQ(Seen().number, -1);        // what value_or was given
  }

  // ── the refusals ─────────────────────────────────────────────────────

  TEST(InlineSubcommands, TooFewWordsNamesTheParameterThatRanOut)
  {
    Fresh();
    App app;

    try {
      oxbox::cli::Apply(app, Args{ "chat" });
      FAIL() << "expected a MissingArgument";
    } catch (oxbox::cli::MissingArgument const& failure) {
      // named the way the help screen spells it, dashes and all
      EXPECT_EQ(failure.parameter, "say-what");
      EXPECT_EQ(failure.description, "the words to say");
    }
    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineSubcommands, TooManyWordsNamesTheFirstOneWithNowhereToGo)
  {
    Fresh();
    App app;

    try {
      oxbox::cli::Apply(app, Args{ "chat", "hello", "spare" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "spare");
    }
    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineSubcommands, AWrongArityLineIsAUsageErrorAndNothingRan)
  {
    Fresh();
    App app;
    Args const line{ "chat" };

    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main(app, line) };
    std::fflush(stderr);
    auto const complained{ testing::internal::GetCapturedStderr() };

    EXPECT_EQ(result.Status(), CliStatus::USAGE_ERROR);
    EXPECT_EQ(result.Code(), oxbox::cli::USAGE_EXIT_CODE);
    EXPECT_NE(complained.find("say-what"), std::string::npos);
    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineSubcommands, AValueThatWillNotConvertRefusesTheLineBeforeItRuns)
  {
    Fresh();
    App app;

    EXPECT_THROW(oxbox::cli::Apply(app, Args{ "maybe", "now", "abc" }),
                 oxbox::utilities::TypeMismatch);
    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineSubcommands, AWordThatNamesNoSubcommandIsRefusedByName)
  {
    Fresh();
    App app;

    try {
      oxbox::cli::Apply(app, Args{ "chatt", "hello" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "chatt");
    }
    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineSubcommands, InitializeIsNotDispatchableAsABareWord)
  {
    // nothing but the by-name exclusion keeps this from bringing the world up
    Fresh();
    App app;

    for (auto const typed : { "initialize", "Initialize" }) {
      try {
        oxbox::cli::Apply(app, Args{ typed });
        FAIL() << "expected an UnexpectedArgument for '" << typed << "'";
      } catch (oxbox::cli::UnexpectedArgument const& failure) {
        EXPECT_EQ(failure.argument, typed);
      }
    }
    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineSubcommands, AMethodThatAnswersAnIntIsNotDispatchableEither)
  {
    Fresh();
    App app;

    try {
      oxbox::cli::Apply(app, Args{ "status" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "status");
    }
    EXPECT_TRUE(Seen().steps.empty());
  }

  // ── the segment: a leaf with no options, read by the one grammar ─────

  TEST(InlineSubcommands, AnOptionAfterTheWordIsUnknownBecauseTheLeafHasNone)
  {
    Fresh();
    App app;

    try {
      oxbox::cli::Apply(app, Args{ "chat", "--label=given", "hi" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.option, "label");
      EXPECT_TRUE(failure.suggestion.empty());
    }
    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineSubcommands, TheSentinelStillMakesAnOptionLookingTokenPositional)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "chat", "--", "--not-an-option" }) };

    EXPECT_EQ(result.Code(), 11);
    EXPECT_EQ(Seen().steps, Strings({ "app", "chat:--not-an-option" }));
  }

  // ── depth: the same spelling on a nested layer ───────────────────────

  TEST(InlineSubcommands, AnInlineLeafOnANestedLayerIsReachedByTheSameDescent)
  {
    // the leaf reads the option of the layer it hangs off, not the root's
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "--mid", "--volume=3", "murmur", "psst" }) };

    EXPECT_EQ(Seen().steps, Strings({ "app", "mid", "murmur:psst" }));
    EXPECT_EQ(Seen().number, 3);
    EXPECT_EQ(result.Code(), 29);
  }

  TEST(InlineSubcommands, AFactoryMayAlsoLeadToAnInlineLeaf)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "reach-mid", "murmur", "quiet" }) };

    EXPECT_EQ(Seen().steps, Strings({ "app", "mid", "murmur:quiet" }));
    EXPECT_EQ(result.Code(), 29);
  }

  TEST(InlineSubcommands, AnOwnerThatRefusesTheTorchStopsTheInlineLeafToo)
  {
    // an inline leaf is below the chain, not beside it, and brings nothing up
    Fresh();
    Seen().mid_code = 4;
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "--mid", "murmur", "psst" }) };

    EXPECT_EQ(Seen().steps, Strings({ "app", "mid" }));   // no murmur
    EXPECT_EQ(result.Code(), 4);
    EXPECT_EQ(result.Message(), "the middle refused");
    EXPECT_EQ(result.Status(), CliStatus::RAN);
  }

  // ── the screens ──────────────────────────────────────────────────────

  TEST(InlineHelp, TheParentsSubcommandRowsIncludeTheInlineNameAndItsComment)
  {
    auto const screen{ oxbox::cli::FormatHelp<App>("app") };

    EXPECT_NE(screen.find("Subcommands:"), std::string::npos);
    EXPECT_NE(screen.find("--mid"), std::string::npos);       // the member
    EXPECT_NE(screen.find("reach-mid"), std::string::npos);   // the factory
    EXPECT_NE(screen.find("chat"), std::string::npos);        // the inline one
    EXPECT_NE(screen.find("say one thing and stop"), std::string::npos);

    EXPECT_EQ(screen.find("status"), std::string::npos);
    EXPECT_EQ(screen.find("Initialize"), std::string::npos);
  }

  TEST(InlineHelp, TheLeafsOwnScreenShowsItsArgumentsAndTheChainAboveIt)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "chat", "--help" }, "app") };
    auto const screen{ result.Message() };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_EQ(result.Code(), 0);

    EXPECT_NE(screen.find("usage: app [options] chat [options] <say-what>"),
              std::string_view::npos);

    EXPECT_NE(screen.find("Arguments:"), std::string_view::npos);
    EXPECT_NE(screen.find("say-what"), std::string_view::npos);
    EXPECT_NE(screen.find("the words to say"), std::string_view::npos);

    // the one option a leaf with no options still accepts
    EXPECT_NE(screen.find("--help"), std::string_view::npos);

    EXPECT_NE(screen.find("app options:"), std::string_view::npos);
    EXPECT_NE(screen.find("--label"), std::string_view::npos);

    // an inline subcommand has nowhere further to go
    EXPECT_EQ(screen.find("Subcommands:"), std::string_view::npos);

    EXPECT_TRUE(Seen().steps.empty());
  }

  TEST(InlineHelp, TheUsageLineReadsTheIntakeRangeOfEveryParameterShape)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "blend", "--help" }, "app") };

    EXPECT_NE(result.Message().find(
      "usage: app [options] blend [options] <pair> <both> [the-rest...]"),
      std::string_view::npos);
  }

  TEST(InlineHelp, AnOptionalParameterIsBracketedOnTheUsageLine)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "maybe", "--help" }, "app") };

    EXPECT_NE(result.Message().find(
      "usage: app [options] maybe [options] <first> [second]"),
      std::string_view::npos);
  }

  TEST(InlineHelp, ANestedInlineLeafNamesEveryLayerAboveIt)
  {
    Fresh();
    App app;

    auto const result{ oxbox::cli::Apply(
      app, Args{ "--mid", "murmur", "--help" }, "app") };
    auto const screen{ result.Message() };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(screen.find(
      "usage: app [options] --mid [options] murmur [options] <word>"),
      std::string_view::npos);

    EXPECT_NE(screen.find("--mid options:"), std::string_view::npos);
    EXPECT_NE(screen.find("--volume"), std::string_view::npos);
    EXPECT_NE(screen.find("app options:"), std::string_view::npos);

    EXPECT_TRUE(Seen().steps.empty());
  }

  // ── and the same, driven by the generated schemes ────────────────────

  TEST(GeneratedInline, TheSpecApplicationRunsAnInlineSubcommandFromADeclaration)
  {
    // nothing but a declaration and the comments beside it, all generated
    oxbox::cli::HelloWorldCli app;

    testing::internal::CaptureStdout();
    auto const result{ oxbox::cli::Apply(
      app, Args{ "say-greeting", "ada" }, "hello-world") };
    auto const printed{ testing::internal::GetCapturedStdout() };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 0);
    EXPECT_EQ(printed, "hello, ada\n");
  }

  TEST(GeneratedInline, TheSpecInlineSubcommandReadsTheOptionDeclaredAboveIt)
  {
    oxbox::cli::HelloWorldCli app;

    testing::internal::CaptureStdout();
    auto const result{ oxbox::cli::Apply(
      app, Args{ "--greeting-language=fr", "say-greeting", "ada", "2" },
      "hello-world") };
    auto const printed{ testing::internal::GetCapturedStdout() };

    EXPECT_EQ(result.Code(), 0);
    EXPECT_EQ(printed, "bonjour, ada\nbonjour, ada\n");
  }

  TEST(GeneratedInline, TheSpecScreenListsAllThreeSpellingsAndTheLeafsArguments)
  {
    auto const listed{ oxbox::cli::FormatHelp<oxbox::cli::HelloWorldCli>(
      "hello-world") };

    EXPECT_NE(listed.find("--say-hello"), std::string::npos);
    EXPECT_NE(listed.find("print-version-positional"), std::string::npos);
    EXPECT_NE(listed.find("say-greeting"), std::string::npos);
    EXPECT_NE(listed.find("Greet somebody by name"), std::string::npos);

    oxbox::cli::HelloWorldCli app;
    auto const leaf{ oxbox::cli::Apply(
      app, Args{ "say-greeting", "--help" }, "hello-world") };

    EXPECT_EQ(leaf.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(leaf.Message().find(
      "usage: hello-world [options] say-greeting [options] "
      "<to-whom> [how-often]"), std::string_view::npos);
    EXPECT_NE(leaf.Message().find("who is being greeted"),
              std::string_view::npos);
  }
}
