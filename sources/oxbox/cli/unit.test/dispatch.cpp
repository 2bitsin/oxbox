// Descending: a subcommand reached by its trigger or by a bare word takes
// the rest of the line, and the layer it was reached through only steps.

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/dispatch-cli.inc"
#include "oxbox/cli/unit.test/hello-world-cli.hpp"
#include "oxbox/cli/unit.test/print-version-cli.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <string>
#include <vector>

namespace
{
  using oxbox::cli::CliStatus;
  using oxbox::cli::unit_test::Root;
  using oxbox::cli::unit_test::Trace;
  using Args    = std::vector<std::string_view>;
  using Strings = std::vector<std::string>;

  // ── member subcommands: --member-name ends the parent's line ──────────

  TEST(Subcommands, AMemberSubcommandTakesEverythingAfterItsTrigger)
  {
    Trace trace;
    Root  root{ &trace };

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--nested", "--depth=3", "hello" }) };

    EXPECT_EQ(trace.ran, "sub");            // the sub's own operator()
    EXPECT_EQ(trace.word, "hello");         // the sub's own positional
    EXPECT_EQ(trace.number, 3);             // the sub's own option
    EXPECT_EQ(result.Code(), 3);            // and the sub's own result
  }

  TEST(Subcommands, OptionsBeforeTheTriggerStillFillTheParent)
  {
    // they are state the subcommand may go on to read; the trigger ends
    // the parent's line, not the parent's part in the run
    Trace trace;
    Root  root{ &trace };

    oxbox::cli::Apply(root, Args{ "--label=given", "--nested", "hello" });

    EXPECT_EQ(root.label, "given");
  }

  TEST(Subcommands, TheParentsInitializeRunsBeforeTheSubcommandsAction)
  {
    // the step is Initialize, not operator(): only the destination's
    // action runs, so `ran` is the sub's and `label` is what the step wrote
    Trace trace;
    Root  root{ &trace };

    oxbox::cli::Apply(root, Args{ "--nested", "hello" });

    EXPECT_EQ(trace.ran, "sub");
    EXPECT_EQ(trace.label, "none");     // the root's Initialize wrote this
  }

  TEST(Subcommands, TheParentsOwnActionNeverRunsWhenTheLineGoesPastIt)
  {
    // Root's operator() would set `ran` to "root", and nothing ever does
    Trace trace;
    Root  root{ &trace };

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--nested", "hello" }) };

    EXPECT_NE(trace.ran, "root");
    EXPECT_EQ(result.Code(), 3);        // the sub's, not the root's 7
  }

  TEST(Subcommands, AMemberSubcommandGivenAValueIsRefused)
  {
    Trace trace;
    Root  root{ &trace };

    try {
      oxbox::cli::Apply(root, Args{ "--nested=1" });
      FAIL() << "expected a SubcommandTakesNoValue";
    } catch (oxbox::cli::SubcommandTakesNoValue const& failure) {
      EXPECT_EQ(failure.option, "nested");
    }
  }

  // ── method subcommands: a bare word, in the first slot only ───────────

  TEST(Subcommands, ABareWordDispatchesToAZeroParameterCommandFactory)
  {
    Trace trace;
    Root  root{ &trace };

    auto const result{ oxbox::cli::Apply(
      root, Args{ "perform-action", "hello" }) };

    EXPECT_EQ(trace.ran, "sub");
    EXPECT_EQ(trace.word, "hello");
    EXPECT_EQ(result.Code(), 3);
  }

  TEST(Subcommands, TheFactoryIsAskedOfAParentWhoseOptionsAreAlreadyIn)
  {
    Trace trace;
    Root  root{ &trace };

    oxbox::cli::Apply(root, Args{ "--label=given", "perform-action", "hi" });

    EXPECT_EQ(trace.label, "given");
  }

  TEST(Subcommands, AReflectedMethodThatDoesNotReturnACommandIsNotOne)
  {
    Trace trace;
    Root  root{ &trace };

    oxbox::cli::Apply(root, Args{ "helper" });

    EXPECT_EQ(trace.ran, "root");
    EXPECT_EQ(trace.word, "helper");
  }

  TEST(Subcommands, TheSameWordAfterAPositionalIsJustAPositional)
  {
    // once an argument has been taken the shape of the line is settled
    Trace trace;
    Root  root{ &trace };

    oxbox::cli::Apply(root, Args{ "first", "perform-action" });

    EXPECT_EQ(trace.ran, "root");
    EXPECT_EQ(trace.word, "first");
    EXPECT_EQ(trace.rest, Strings({ "perform-action" }));
  }

  TEST(Subcommands, TheSameWordAfterTheSentinelIsJustAPositionalToo)
  {
    Trace trace;
    Root  root{ &trace };

    oxbox::cli::Apply(root, Args{ "--", "perform-action" });

    EXPECT_EQ(trace.ran, "root");
    EXPECT_EQ(trace.word, "perform-action");
  }

  // ── recursion: every rule holds at every depth ────────────────────────

  TEST(Subcommands, HelpAfterATriggerIsTheSubcommandsHelpAndTheWholeChains)
  {
    // the line still accepts the parent's options after `--nested`, so
    // they are on the screen too, in a section of their own
    Trace trace;
    Root  root{ &trace };

    auto const result{ oxbox::cli::Apply(root, Args{ "--nested", "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(result.Message().find("--depth"), std::string_view::npos);
    EXPECT_NE(result.Message().find("--label"), std::string_view::npos);
    // and they are in a section of the root's -- the layer they belong
    // to, which no word on the line reached, so it is titled by the
    // stand-in the caller left the program name at
    EXPECT_NE(result.Message().find("root options:"),
              std::string_view::npos);
    EXPECT_TRUE(trace.ran.empty());
  }

  TEST(Subcommands, HelpBeforeATriggerIsStillTheParentsHelp)
  {
    Trace trace;
    Root  root{ &trace };

    auto const result{ oxbox::cli::Apply(root, Args{ "--help", "--nested" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(result.Message().find("--label"), std::string_view::npos);
  }

  // ── the same machinery, driven by the generated schemes ───────────────

  TEST(Generated, TheSpecApplicationIsEnteredByEitherSpellingOfOneSubcommand)
  {
    // a command with required positionals is a leaf and one with
    // subcommands is a layer; no command in the spec is both
    oxbox::cli::HelloWorldCli app;

    EXPECT_EQ(&oxbox::cli::Command::Get<oxbox::cli::PrintVersion>(),
              &app.print_version_switch);

    testing::internal::CaptureStdout();
    auto const dashed{ oxbox::cli::Apply(
      app, Args{ "--print-version-switch" }) };
    auto const bare{ oxbox::cli::Apply(
      app, Args{ "print-version-positional" }) };
    auto const printed{ testing::internal::GetCapturedStdout() };

    EXPECT_EQ(dashed.Status(), CliStatus::RAN);
    EXPECT_EQ(dashed.Code(), 0);
    EXPECT_EQ(bare.Status(), CliStatus::RAN);
    EXPECT_EQ(bare.Code(), 0);
    // one version line per spelling: both of them ran the same command
    EXPECT_EQ(printed, "0.0.1-alpha\n0.0.1-alpha\n");
  }

  TEST(Generated, TheSpecLeafRefusesAWordItsSignatureCannotTake)
  {
    // the layer takes no positionals of its own, so a word before the
    // trigger belongs to nobody and is refused by name
    oxbox::cli::HelloWorldCli app;

    try {
      oxbox::cli::Apply(app, Args{ "stray", "--print-version-switch" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "stray");
    }
  }
}

// ── the root is a singleton too ────────────────────────────────────────

namespace oxbox::cli::unit_test
{
  // the object Main<App> parses into is the one Command::Get<App>() hands
  // out, so a subcommand reads the root with no stack and no parameter
  struct RootReader : Command
  {
    friend constexpr auto reflect_scheme(RootReader*);
    auto operator() () const -> CliResult;
  };

  struct SingletonRoot : Command
  {
    friend constexpr auto reflect_scheme(SingletonRoot*);
    int shared{ 0 };                     /* configuration every verb needs */
    auto dig() const -> RootReader& { return Command::Get<RootReader>(); }
  };

  constexpr auto reflect_scheme(RootReader*)
  {
    return ::reflect::class_scheme<>{ };
  }

    // without this the operator() is invisible, and a destination with no
    // visible action falls to its help screen and exits USAGE_EXIT_CODE
  constexpr auto reflect_call_scheme(RootReader*)
  {
    return ::reflect::call_scheme<>{ };
  }

  constexpr auto reflect_scheme(SingletonRoot*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"shared", &SingletonRoot::shared,
                               "configuration every verb needs", false>>{ };
  }

  constexpr auto reflect_interface_scheme(SingletonRoot*)
  {
    return ::reflect::interface_scheme<
      ::reflect::method_scheme<"dig", &SingletonRoot::dig, "reads the root">>{ };
  }

  // The whole point: the subcommand asks Get for the root and finds the
  // values the line put there.
  auto RootReader::operator() () const -> CliResult
  {
    return CliResult{ Command::Get<SingletonRoot>().shared };
  }
}

namespace
{
  using oxbox::cli::unit_test::SingletonRoot;

  TEST(Singletons, TheRootMainParsesIntoIsTheRootGetHandsOut)
  {
    std::vector<std::string_view> const line{ "--shared", "41", "dig" };
    EXPECT_EQ(oxbox::cli::Main<SingletonRoot>(line).Code(), 41);
  }

  TEST(Singletons, ASecondRunStartsFromWhatTheFirstLeftBehind)
  {
    // the root is a noun too: its state survives between runs in a thread,
    // and ctest gives every test its own process
    std::vector<std::string_view> const first{ "--shared", "41", "dig" };
    std::vector<std::string_view> const again{ "dig" };
    ASSERT_EQ(oxbox::cli::Main<SingletonRoot>(first).Code(), 41);
    EXPECT_EQ(oxbox::cli::Main<SingletonRoot>(again).Code(), 41);
  }
}
