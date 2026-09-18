// The dispatch chain: root to destination, every layer on the way down.
// Initialize is what a layer does to be passed through, operator() what a
// command does when it is what the line asked for.

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
  using oxbox::cli::chain_test::Doer;
  using oxbox::cli::chain_test::Eager;
  using oxbox::cli::chain_test::Fresh;
  using oxbox::cli::chain_test::Group;
  using oxbox::cli::chain_test::Leaf;
  using oxbox::cli::chain_test::Mid;
  using oxbox::cli::chain_test::Root;
  using oxbox::cli::chain_test::Speller;
  using Args    = std::vector<std::string_view>;
  using Strings = std::vector<std::string>;

  // ── which function the framework looks for, as a compile-time claim ──
  // the probe is a concept over the type: no base class, no macro, no
  // scheme entry
  static_assert( oxbox::cli::Initializes<Mid>);
  static_assert(!oxbox::cli::HasInitialize<Leaf>);
  static_assert( oxbox::cli::Initializes<Doer>);
  static_assert(!oxbox::cli::HasInitialize<Group>);

  static_assert(!::reflect::callable_reflected<Mid>);
  static_assert( ::reflect::callable_reflected<Leaf>);
  static_assert( ::reflect::callable_reflected<Doer>);
  static_assert(!::reflect::callable_reflected<Group>);

  // ── the torch: every layer initializes, root first ───────────────────

  TEST(Chain, EveryLayerInitializesInOrderFromRootToTheDestination)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "--leaf", "tip" }) };

    // Leaf has no Initialize, so nothing of its own precedes its action
    EXPECT_EQ(Chain().steps, Strings({ "root", "mid", "leaf:tip" }));
    // the answer is the destination's; the two above only opened the way
    EXPECT_EQ(result.Code(), 17);
    EXPECT_EQ(result.Status(), CliStatus::RAN);
  }

  TEST(Chain, ADestinationInitializesItselfBeforeItActs)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--doer", "now" }) };

    EXPECT_EQ(Chain().steps, Strings({ "root", "doer-init", "doer:now" }));
    EXPECT_EQ(result.Code(), 23);
  }

  TEST(Chain, ADestinationsFailedInitializeMeansItsActionNeverRuns)
  {
    // its refusal is the run's answer, rather than its operator()'s
    Fresh();
    Chain().doer_code = 5;
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--doer", "now" }) };

    EXPECT_EQ(Chain().steps, Strings({ "root", "doer-init" }));
    EXPECT_EQ(result.Code(), 5);
    EXPECT_EQ(result.Message(), "the doer was not ready");
    EXPECT_EQ(result.Status(), CliStatus::RAN);
  }

  TEST(Chain, TheLeafStillBindsThePositionalsOfItsOwnSegment)
  {
    Fresh();
    Root root;

    oxbox::cli::Apply(root, Args{ "--mid", "--level=4", "--leaf",
                                  "--tone=loud", "spoken" });

    EXPECT_EQ(Chain().steps, Strings({ "root", "mid", "leaf:spoken" }));
    EXPECT_EQ(root.mid.level, 4);        // each layer's options are its own
    EXPECT_EQ(root.mid.leaf.tone, "loud");
  }

  TEST(Chain, ALayerWithNoInitializeIsSkippedRatherThanRefused)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--group", "--leaf", "quiet" }) };

    EXPECT_EQ(Chain().steps, Strings({ "root", "leaf:quiet" }));
    EXPECT_EQ(result.Code(), 17);
  }

  // ── operator() is the action, and only the destination's runs ────────

  TEST(Chain, ALayerPassedThroughNeverRunsItsOperator)
  {
    // Eager declares an operator() and also carries a subcommand
    Fresh();
    Eager eager;

    auto const result{ oxbox::cli::Apply(eager, Args{ "--leaf", "tip" }) };

    EXPECT_EQ(Chain().steps, Strings({ "eager-init", "leaf:tip" }));
    EXPECT_EQ(result.Code(), 17);
  }

  TEST(Chain, TheSameLayerAsTheDestinationDoesRunIt)
  {
    // the same fixture and the same operator(), reached as the destination
    Fresh();
    Eager eager;

    auto const result{ oxbox::cli::Apply(eager, Args{ }) };

    EXPECT_EQ(Chain().steps, Strings({ "eager-init", "eager-action" }));
    EXPECT_EQ(result.Code(), 41);
    EXPECT_EQ(result.Status(), CliStatus::RAN);
  }

  // ── a layer blocks the chain by failing ──────────────────────────────

  TEST(Chain, AFailingMiddleStopsTheChainWhereItStands)
  {
    Fresh();
    Chain().mid_code = 4;
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "--leaf", "tip" }) };

    EXPECT_EQ(Chain().steps, Strings({ "root", "mid" }));
    EXPECT_EQ(result.Code(), 4);
    EXPECT_EQ(result.Message(), "the middle refused");
    EXPECT_EQ(result.Status(), CliStatus::RAN);
  }

  TEST(Chain, AFailingRootStopsTheChainAndIsTheAnswer)
  {
    Fresh();
    Chain().root_code   = 3;
    Chain().root_reason = "could not connect to api";
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "--level=4", "--leaf", "tip" }) };

    EXPECT_EQ(Chain().steps, Strings({ "root" }));
    EXPECT_EQ(result.Code(), 3);
    EXPECT_EQ(result.Message(), "could not connect to api");
    // it ran: a usage error is the other thing, and means nothing ran
    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_NE(result.Status(), CliStatus::USAGE_ERROR);
    EXPECT_NE(result.Code(), oxbox::cli::USAGE_EXIT_CODE);
  }

  TEST(Chain, AChildOfAFailingParentIsNeverEvenParsed)
  {
    // `--level=4` is in the middle layer's segment, and a blocked chain is
    // blocked before the parse rather than after it
    Fresh();
    Chain().root_code   = 3;
    Chain().root_reason = "could not connect to api";
    Root root;

    oxbox::cli::Apply(root, Args{ "--mid", "--level=4", "--leaf", "tip" });

    EXPECT_EQ(root.mid.level, 0);        // still the default
    EXPECT_EQ(root.mid.leaf.tone, "plain");
  }

  TEST(Chain, MainReportsAFailedResultsReasonOnStderr)
  {
    Fresh();
    Chain().root_code   = 3;
    Chain().root_reason = "could not connect to api";

    Args const line{ "--mid", "--leaf", "tip" };

    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main<Root>(line) };
    std::fflush(stderr);
    auto const complained{ testing::internal::GetCapturedStderr() };

    EXPECT_EQ(result.Code(), 3);
    EXPECT_EQ(complained, "error: could not connect to api\n");
    EXPECT_EQ(Chain().steps, Strings({ "root" }));
  }

  TEST(Chain, AnOrdinaryRunSaysNothingOnStderr)
  {
    Fresh();

    Args const line{ "--mid", "--leaf", "tip" };

    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main<Root>(line) };
    std::fflush(stderr);
    auto const complained{ testing::internal::GetCapturedStderr() };

    EXPECT_EQ(result.Code(), 17);
    EXPECT_TRUE(result.Message().empty());
    EXPECT_EQ(complained, "");
  }

  TEST(Chain, AThrowingRootBlocksTheChainTheSameWay)
  {
    Fresh();
    Chain().root_throws = true;
    Root root;

    EXPECT_THROW(oxbox::cli::Apply(root, Args{ "--mid", "--leaf", "tip" }),
                 oxbox::utilities::ParseError);

    EXPECT_EQ(Chain().steps, Strings({ "root" }));
    EXPECT_EQ(root.mid.level, 0);        // the child never parsed
  }

  TEST(Chain, MainMapsAThrowingRootToTheUsageErrorItAlreadyMapped)
  {
    // the ParseError family reaches Main's own handler, and a chain changes
    // nothing about what it does there
    Fresh();
    Chain().root_throws = true;

    Args const line{ "--mid", "--leaf", "tip" };

    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main<Root>(line) };
    std::fflush(stderr);
    auto const complained{ testing::internal::GetCapturedStderr() };

    EXPECT_EQ(result.Status(), CliStatus::USAGE_ERROR);
    EXPECT_EQ(result.Code(), oxbox::cli::USAGE_EXIT_CODE);
    EXPECT_EQ(complained, "error: could not connect to api\n");
    EXPECT_EQ(Chain().steps, Strings({ "root" }));
  }

  // ── only the destination is given positionals ────────────────────────

  TEST(Chain, AWordBeforeATriggerBelongsToNobodyAndIsRefused)
  {
    Fresh();
    Root root;

    try {
      oxbox::cli::Apply(root, Args{ "stray", "--mid", "--leaf", "tip" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "stray");
    }

    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, TheSameRefusalHoldsAtEveryDepthNotJustTheRoot)
  {
    // "stray" is in the middle layer's segment, which dispatches too
    Fresh();
    Root root;

    try {
      oxbox::cli::Apply(root, Args{ "--mid", "stray", "--leaf", "tip" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "stray");
    }

    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(Chain, ALayerThatDispatchesDeclaresNoSignatureToBeStarvedOf)
  {
    // Mid declares no operator() at all, and Initialize takes nothing
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--mid", "--leaf" }) };

    EXPECT_EQ(Chain().steps, Strings({ "root", "mid", "leaf:-" }));
    EXPECT_EQ(result.Code(), 17);
  }

  // ── the method form ──────────────────────────────────────────────────

  TEST(Chain, TheFactoryIsAskedForTheChildOnlyAfterTheParentSucceeded)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "forge-mid", "--leaf", "tip" }) };

    EXPECT_EQ(Chain().factories, 1);
    EXPECT_EQ(Chain().steps, Strings({ "root", "mid", "leaf:tip" }));
    EXPECT_EQ(result.Code(), 17);
  }

  TEST(Chain, AFailingParentIsNeverEvenAskedForTheChild)
  {
    Fresh();
    Chain().root_code   = 3;
    Chain().root_reason = "could not connect to api";
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "forge-mid", "--leaf", "tip" }) };

    EXPECT_EQ(Chain().factories, 0);     // the whole ordering claim
    EXPECT_EQ(Chain().steps, Strings({ "root" }));
    EXPECT_EQ(result.Code(), 3);
  }

  TEST(Chain, OneSubcommandIsReachedEndToEndByEitherOfItsSpellings)
  {
    Fresh();
    Speller speller;

    auto const dashed{ oxbox::cli::Apply(
      speller, Args{ "--by-switch", "here" }) };
    auto const bare{ oxbox::cli::Apply(
      speller, Args{ "by-word", "there" }) };

    EXPECT_EQ(Chain().steps, Strings({ "speller", "leaf:here",
                                       "speller", "leaf:there" }));
    EXPECT_EQ(dashed.Code(), 17);
    EXPECT_EQ(bare.Code(), 17);
    EXPECT_EQ(&speller.by_switch, &oxbox::cli::Command::Get<Leaf>());
    EXPECT_EQ(&speller.by_word(), &oxbox::cli::Command::Get<Leaf>());
  }
}
