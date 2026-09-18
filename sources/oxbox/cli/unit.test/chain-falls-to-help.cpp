// A destination the line reached but that has nothing to run: its own
// help screen stands in for the action, and the run answers usage.

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/chain-cli.inc"

#include <gtest/gtest.h>

#include <cstdio>
#include <string_view>
#include <string>
#include <vector>

namespace
{
  using oxbox::cli::CliStatus;
  using oxbox::cli::chain_test::Chain;
  using oxbox::cli::chain_test::Fresh;
  using oxbox::cli::chain_test::Root;
  using Args    = std::vector<std::string_view>;

  // ── a destination with no action falls to its help screen ────────────

  TEST(FallsToHelp, ABareLineAtAnActionlessRootRendersItsOwnScreen)
  {
    // the root carries verbs and no operator(): the line arrived somewhere
    // and named nothing to do
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ }, "walker") };

    EXPECT_EQ(result.Status(), CliStatus::NO_ACTION);
    EXPECT_TRUE(result.ShowsScreen());
    EXPECT_NE(result.Message().find("Subcommands:"), std::string_view::npos);
    EXPECT_NE(result.Message().find("--mid"),        std::string_view::npos);
    EXPECT_NE(result.Message().find("--endpoint"),   std::string_view::npos);
  }

  TEST(FallsToHelp, TheCodeSaysUsageEvenThoughTheScreenSaysHelp)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ }) };

    EXPECT_EQ(result.Code(), oxbox::cli::USAGE_EXIT_CODE);
    EXPECT_EQ(result.Code(), 2);
    EXPECT_NE(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(result.Status(), CliStatus::USAGE_ERROR);
    EXPECT_NE(result.Status(), CliStatus::RAN);
  }

  TEST(FallsToHelp, AnExplicitHelpIsStillTheHelpStatusAndStillExitsZero)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_EQ(result.Code(), 0);
    EXPECT_TRUE(result.ShowsScreen());
  }

  TEST(FallsToHelp, TheScreenIsTheSameOneTheExplicitHelpWouldHaveRendered)
  {
    Fresh();
    Root root;

    auto const fell {  oxbox::cli::Apply(root, Args{ },         "walker") };
    auto const asked{ oxbox::cli::Apply(root, Args{ "--help" }, "walker") };

    EXPECT_EQ(fell.Message(), asked.Message());
    EXPECT_NE(fell.Code(), asked.Code());
  }

  TEST(FallsToHelp, ABareLineAtAnActionlessMiddleRendersTheWholeChain)
  {
    // mid's own options unadorned, the root's in a section of their own
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--mid" }) };

    EXPECT_EQ(result.Status(), CliStatus::NO_ACTION);
    EXPECT_EQ(result.Code(), 2);
    EXPECT_NE(result.Message().find("--level"),       std::string_view::npos);
    EXPECT_NE(result.Message().find("root options:"), std::string_view::npos);
    EXPECT_NE(result.Message().find("--endpoint"),    std::string_view::npos);
  }

  TEST(FallsToHelp, NothingRanAndNothingInitializedOnTheWayToThatScreen)
  {
    // whether the destination has an action is a question about its type
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--mid" }) };

    EXPECT_EQ(result.Status(), CliStatus::NO_ACTION);
    EXPECT_TRUE(Chain().steps.empty());
    EXPECT_EQ(Chain().factories, 0);
  }

  TEST(FallsToHelp, AStrayWordAtAnActionlessDestinationIsStillAnError)
  {
    // only a complete line that still names nothing falls to help
    Fresh();
    Root root;

    try {
      oxbox::cli::Apply(root, Args{ "bogus" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "bogus");
    }

    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(FallsToHelp, MainPrintsTheScreenOnStdoutAndSaysNothingOnStderr)
  {
    Fresh();

    Args const line{ "--mid" };

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main<Root>(line) };
    std::fflush(stdout);
    std::fflush(stderr);
    auto const printed  { testing::internal::GetCapturedStdout() };
    auto const complained{ testing::internal::GetCapturedStderr() };

    EXPECT_EQ(result.Code(), 2);
    EXPECT_EQ(complained, "");
    EXPECT_NE(printed.find("--level"), std::string::npos);
    EXPECT_EQ(printed, result.Message());
  }
}
