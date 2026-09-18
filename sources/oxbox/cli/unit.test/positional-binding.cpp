// Positional binding: the words a line has left over become the arguments
// of the operator() it reached, each parameter taking what its type is worth.

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/dispatch-cli.inc"
#include "oxbox/cli/unit.test/hello-world-cli.hpp"
#include "oxbox/cli/unit.test/print-version-cli.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <string>
#include <vector>

namespace
{
  using oxbox::cli::CliStatus;
  using oxbox::cli::unit_test::Echo;
  using oxbox::cli::unit_test::Greet;
  using oxbox::cli::unit_test::Shade;
  using oxbox::cli::unit_test::Trace;
  using Args    = std::vector<std::string_view>;
  using Strings = std::vector<std::string>;

  // ── binding: the parameter's type says how much it takes ─────────────

  TEST(Positionals, EachParameterShapeTakesWhatItsTypeIsWorth)
  {
    Trace trace;
    Echo  echo{ &trace };

    auto const result{ oxbox::cli::Apply(
      echo, Args{ "1", "2", "3", "word", "4", "x", "y" }) };

    EXPECT_EQ(result.Code(), 11);
    EXPECT_EQ(trace.ran, "echo");
    EXPECT_EQ(trace.number, 1);                       // scalar: one token
    EXPECT_EQ(trace.pair, (std::array<int, 2>{ 2, 3 }));  // array: two
    EXPECT_EQ(trace.word, "word");                    // tuple: two more,
    EXPECT_EQ(trace.extra, 4);                        // element by element
  }

  TEST(Positionals, TheTailReceivesExactlyWhatIsLeftAndInOrder)
  {
    Trace trace;
    Echo  echo{ &trace };

    oxbox::cli::Apply(echo, Args{ "1", "2", "3", "word", "4", "x", "y", "z" });

    EXPECT_EQ(trace.rest, Strings({ "x", "y", "z" }));
  }

  TEST(Positionals, TheTailIsEmptyWhenTheOthersTookEverything)
  {
    Trace trace;
    Echo  echo{ &trace };

    oxbox::cli::Apply(echo, Args{ "1", "2", "3", "word", "4" });

    EXPECT_TRUE(trace.rest.empty());
  }

  TEST(Positionals, AValueTheOptionWalkConsumedIsNotAPositional)
  {
    Trace trace;
    Echo  echo{ &trace };

    // the "9" belongs to --leading; it was never on offer to the tail
    oxbox::cli::Apply(echo,
      Args{ "--leading", "9", "1", "2", "3", "word", "4", "x" });

    EXPECT_EQ(echo.leading, 9);
    EXPECT_EQ(trace.number, 1);
    EXPECT_EQ(trace.rest, Strings({ "x" }));
  }

  TEST(Positionals, AnEnumParameterResolvesThroughItsReflectedNames)
  {
    // a positional converts by exactly the vocabulary an option does
    Trace trace;
    Greet greet{ &trace };

    oxbox::cli::Apply(greet, Args{ "ada", "soft" });

    EXPECT_EQ(trace.word, "ada");
    EXPECT_EQ(trace.number, static_cast<int>(Shade::SOFT));
  }

  // ── too few, too many ────────────────────────────────────────────────

  TEST(Positionals, TooFewNamesTheParameterThatRanOut)
  {
    Trace trace;
    Greet greet{ &trace };

    try {
      oxbox::cli::Apply(greet, Args{ "ada" });
      FAIL() << "expected a MissingArgument";
    } catch (oxbox::cli::MissingArgument const& failure) {
      // the parameter is what is missing, and it is named the way the
      // help screen spells it -- underscores and all
      EXPECT_EQ(failure.parameter, "in-shade");
      EXPECT_EQ(failure.description, "which shade to greet in");
      EXPECT_NE(std::string_view{ failure.what() }.find("in-shade"),
                std::string_view::npos);
    }
    EXPECT_TRUE(trace.ran.empty());
  }

  TEST(Positionals, AShapedParameterIsMissingUntilEveryElementIsThere)
  {
    Trace trace;
    Echo  echo{ &trace };

    try {
      oxbox::cli::Apply(echo, Args{ "1", "2", "3", "word" });
      FAIL() << "expected a MissingArgument";
    } catch (oxbox::cli::MissingArgument const& failure) {
      EXPECT_EQ(failure.parameter, "both");
    }
  }

  TEST(Positionals, TooManyNamesTheFirstTokenWithNowhereToGo)
  {
    Trace trace;
    Greet greet{ &trace };

    try {
      oxbox::cli::Apply(greet, Args{ "ada", "soft", "spare", "more" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "spare");
    }
    EXPECT_TRUE(trace.ran.empty());
  }

  TEST(Positionals, ACommandWithNoParametersRefusesEveryArgument)
  {
    // PrintVersion's own comment says so: no parameters, so the first
    // positional is already one too many
    oxbox::cli::PrintVersion version;

    try {
      oxbox::cli::Apply(version, Args{ "spare" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "spare");
    }
  }

  // ── the sentinel ─────────────────────────────────────────────────────

  TEST(Positionals, TheSentinelMakesAnOptionLookingTokenPositional)
  {
    Trace trace;
    Greet greet{ &trace };

    oxbox::cli::Apply(greet, Args{ "--", "--not-an-option", "loud" });

    EXPECT_EQ(trace.word, "--not-an-option");
    EXPECT_EQ(trace.number, static_cast<int>(Shade::LOUD));
  }

  TEST(Positionals, WithoutTheSentinelTheSameTokenIsStillAnOption)
  {
    Trace trace;
    Greet greet{ &trace };

    EXPECT_THROW(oxbox::cli::Apply(greet, Args{ "--not-an-option", "loud" }),
                 oxbox::cli::UnknownOption);
  }

  // ── the same machinery, driven by the generated schemes ───────────────

  TEST(Generated, TheSpecFixtureBindsItsSixDeclaredPositionals)
  {
    oxbox::cli::HelloWorld hello;

    auto const result{ oxbox::cli::Apply(
      hello, Args{ "7", "1.5", "2.5", "word", "3", "4.5", "tail" }) };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 0);
  }

  TEST(Generated, TheSpecFixtureNamesTheParameterItRanOutOf)
  {
    oxbox::cli::HelloWorld hello;

    try {
      oxbox::cli::Apply(hello, Args{ "7", "1.5", "2.5", "word" });
      FAIL() << "expected a MissingArgument";
    } catch (oxbox::cli::MissingArgument const& failure) {
      EXPECT_EQ(failure.parameter, "value3to5");
    }
  }
}
