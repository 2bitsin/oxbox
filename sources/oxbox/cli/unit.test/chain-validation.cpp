// The whole line is judged before any of it runs, so a help anywhere on
// it and a refusal of it alike leave every layer uninitialized.

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/chain-cli.inc"

#include <gtest/gtest.h>

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
  using Strings = std::vector<std::string>;

  // ── help runs nothing, at any depth ──────────────────────────────────

  TEST(Chain, HelpAtTheRootInitializesNothingAnywhere)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, HelpAfterATriggerInitializesNothingAnywhereEither)
  {
    // `--help` here is in the middle layer's segment, and `--endpoint` is
    // the root's option on a line that still accepts it
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--mid", "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(result.Message().find("--level"), std::string_view::npos);
    EXPECT_NE(result.Message().find("--endpoint"), std::string_view::npos);
    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, HelpThreeLayersDownStillRunsNothingAboveIt)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "--leaf", "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(result.Message().find("--tone"), std::string_view::npos);
    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, HelpBelowAMethodTriggerRunsNothingAndNeverAsksTheFactory)
  {
    // pass one reaches the child by naming the method's return type, which
    // calls nothing, and a line asking for help never gets a pass two
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "forge-mid", "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(result.Message().find("--level"), std::string_view::npos);
    EXPECT_TRUE(Chain().steps.empty());
    EXPECT_EQ(Chain().factories, 0);
  }

  // ── the validation pass: the whole line, before any of it ────────────

  TEST(Chain, AnUnknownOptionBelowATriggerRefusesTheLineBeforeAnythingRuns)
  {
    // `--bogus` is in the middle layer's segment
    Fresh();
    Root root;

    try {
      oxbox::cli::Apply(root, Args{ "--mid", "--bogus" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.option, "bogus");
    }

    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, ADeepConversionFailureRunsNothingEither)
  {
    // the scan converts every value it meets and throws the result away
    Fresh();
    Root root;

    EXPECT_THROW(oxbox::cli::Apply(
      root, Args{ "--mid", "--level", "abc", "--leaf", "tip" }),
      oxbox::utilities::TypeMismatch);

    EXPECT_TRUE(Chain().steps.empty());
    EXPECT_EQ(root.mid.level, 0);        // and nothing was written either
  }

  TEST(Chain, ALeafArityFailureRunsNothingAboveIt)
  {
    // two words for one optional parameter would not bind
    Fresh();
    Root root;

    try {
      oxbox::cli::Apply(root, Args{ "--mid", "--leaf", "one", "two" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "two");
    }

    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, ARepeatedOptionThreeLayersDownRunsNothingAboveItEither)
  {
    // the scan counts occurrences as the fill does; it is the fill's loop
    Fresh();
    Root root;

    EXPECT_THROW(oxbox::cli::Apply(
      root, Args{ "--mid", "--leaf", "--tone=a", "--tone=b" }),
      oxbox::cli::RepeatedOption);

    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, AHelpAfterAnUndecidableTokenDoesNotBecomeAHelpScreen)
  {
    // left to right, and the first decisive token wins
    Fresh();
    Root root;

    EXPECT_THROW(oxbox::cli::Apply(root, Args{ "--mid", "--bogus", "--help" }),
                 oxbox::cli::UnknownOption);

    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, GarbageAfterAHelpIsNeverReadAtAll)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "--help", "--bogus" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, AStrayWordBeforeAHelpIsStillTheHelpScreen)
  {
    // a word is the one token that is not decisive where it stands: it may
    // yet be a positional, and that is known only when the segment ends
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "stray", "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, AnOptionValueThatLooksLikeHelpIsStillJustAValue)
  {
    // only parse.hpp's own loop knows `--tone` spends the next argument
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "--level", "4", "--leaf", "--tone", "--help" }) };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 17);
    EXPECT_EQ(root.mid.leaf.tone, "--help");
    EXPECT_EQ(Chain().steps, Strings({ "root", "mid", "leaf:-" }));
  }
}
