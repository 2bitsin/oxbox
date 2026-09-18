// Labels, and the one label that is a marker: `_Label(--)`.
// The fixtures in rest-cli.hpp are generated, so buildutil reading a
// label off the declaration is part of what is under test.

#include "oxbox/cli/main.hpp"
#include "oxbox/cli/parse.hpp"

#include "oxbox/cli/unit.test/rest-cli.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <string>
#include <vector>

namespace
{
  using oxbox::cli::Borrowing;
  using oxbox::cli::CliStatus;
  using oxbox::cli::Command;
  using oxbox::cli::Joining;
  using oxbox::cli::Layered;
  using oxbox::cli::Leaf;
  using oxbox::cli::NearMiss;
  using oxbox::cli::Painter;
  using oxbox::cli::Plain;
  using oxbox::cli::Renamed;
  using oxbox::cli::Tint;
  using oxbox::cli::Wrapper;

  using Args    = std::vector<std::string_view>;
  using Strings = std::vector<std::string>;
  using Views   = std::vector<std::string_view>;

  template <typename CliApp>
  auto Parsed(Args const& args) -> CliApp
  {
    CliApp app;
    oxbox::cli::ParseOptions(app, args);
    return app;
  }

  // ── two names that are nearly one ────────────────────────────────────

  TEST(ADistinctPair, IsNotACollisionAndBothNamesReachTheirMember)
  {
    // The collision refusal is a static_assert and cannot be a test of its
    // own; parsing this command instantiates its walk. The pair that really
    // collides is a tools/negative-compile.sh row.
    auto const parsed{ Parsed<NearMiss>({ "--content-type=text/plain",
                                          "--content-types=text,html" }) };
    EXPECT_EQ(parsed.content_type, "text/plain");
    EXPECT_EQ(parsed.kinds,        "text,html"  );
  }

  // ── a label is the option's external name ────────────────────────────

  TEST(ALabel, IsTheNameTheLineMatches)
  {
    EXPECT_TRUE(Parsed<Wrapper>({ "--secure" }).tls);
  }

  TEST(ALabel, ReplacesTheMemberNameRatherThanAddingToIt)
  {
    EXPECT_THROW(Parsed<Wrapper>({ "--tls" }), oxbox::cli::UnknownOption);
  }

  TEST(ALabel, TakesAValueByEveryFormAnOrdinaryOptionDoes)
  {
    EXPECT_FALSE(Parsed<Wrapper>({ "--secure=false" }).tls);
    EXPECT_TRUE(Parsed<Wrapper>({ "--secure=true" }).tls);
  }

  TEST(ALabel, IsWhatTheHelpScreenPrints)
  {
    auto const screen{ oxbox::cli::FormatHelp<Wrapper>("wrapper") };

    EXPECT_NE(screen.find("--secure"), std::string::npos);
    EXPECT_EQ(screen.find("--tls"), std::string::npos);
  }

  TEST(ALabel, IsWhatASuggestionSpells)
  {
    try {
      Parsed<Wrapper>({ "--secur" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.suggestion, "secure");
      EXPECT_NE(std::string_view{ failure.what() }.find("--secure"),
                std::string_view::npos);
    }
  }

  TEST(ALabel, IsWhatARefusalNamesTheOptionBy)
  {
    try {
      Parsed<Leaf>({ "--depth" });
      FAIL() << "expected a MissingValue";
    } catch (oxbox::cli::MissingValue const& failure) {
      EXPECT_EQ(failure.option, "depth");
    }
  }

  TEST(ALabel, RenamesASubcommandMemberToo)
  {
    Renamed app;
    auto const outcome{ oxbox::cli::ParseOptions(app, Args{ "--kid" }) };

    EXPECT_TRUE(outcome.Dispatches());
    EXPECT_THROW(oxbox::cli::ParseOptions(app, Args{ "--child" }),
                 oxbox::cli::UnknownOption);
  }

  TEST(ALabel, RenamesBothKindsOfSubcommandMethod)
  {
    // the factory returns a Command, the inline subcommand a CliResult
    Renamed app;

    EXPECT_TRUE(oxbox::cli::ParseOptions(app, Args{ "list-them" })
                  .Dispatches());
    EXPECT_TRUE(oxbox::cli::ParseOptions(app, Args{ "say-hi", "ada" })
                  .Dispatches());

    // a bare word that triggers nothing is a positional, not a refusal
    EXPECT_FALSE(oxbox::cli::ParseOptions(app, Args{ "enumerate" })
                   .Dispatches());
  }

  TEST(ALabel, IsWhatTheSubcommandRowsShow)
  {
    auto const screen{ oxbox::cli::FormatHelp<Renamed>("renamed") };

    EXPECT_NE(screen.find("--kid"),   std::string::npos);
    EXPECT_NE(screen.find("list-them"), std::string::npos);
    EXPECT_NE(screen.find("say-hi"),  std::string::npos);
    EXPECT_EQ(screen.find("--child"), std::string::npos);
    EXPECT_EQ(screen.find("enumerate"), std::string::npos);
  }

  // ── a label is the enum value's external name too ────────────────────

  TEST(ALabel, IsWhatAnEnumValueIsWrittenAsOnTheLine)
  {
    EXPECT_EQ(Parsed<Painter>({ "--tint=coral" }).tint, Tint::PALE_RED);
  }

  TEST(ALabel, ReplacesTheEnumeratorNameRatherThanAddingToIt)
  {
    // DEEP_BLUE would spell deep-blue by the ordinary rule
    EXPECT_THROW(Parsed<Painter>({ "--tint=deep-blue" }),
                 oxbox::utilities::TypeMismatch);
  }

  TEST(ALabel, IsWhatTheAcceptedEnumValuesAreListedAs)
  {
    try {
      Parsed<Painter>({ "--tint=nonesuch" });
      FAIL() << "expected a TypeMismatch";
    } catch (oxbox::utilities::TypeMismatch const& failure) {
      std::string_view const said{ failure.what() };
      EXPECT_NE(said.find("ocean"), std::string_view::npos);
      EXPECT_NE(said.find("coral"), std::string_view::npos);
      EXPECT_EQ(said.find("deep-blue"), std::string_view::npos);
    }
  }

  TEST(ALabel, IsWhatAnEnumDefaultIsShownBackAs)
  {
    auto const screen{ oxbox::cli::FormatHelp<Painter>("painter") };

    EXPECT_NE(screen.find("one of: ocean, coral"), std::string::npos);
    EXPECT_NE(screen.find("(default: ocean)"), std::string::npos);
    EXPECT_EQ(screen.find("deep-blue"), std::string::npos);
  }

  // ── a label names an operator() parameter's slot ─────────────────────

  TEST(ALabel, IsWhatTheUsageLineCallsAPositionalSlot)
  {
    auto const screen{ oxbox::cli::FormatHelp<Painter>("painter") };

    EXPECT_NE(screen.find("usage: painter [options] <page>"),
              std::string::npos);
    EXPECT_EQ(screen.find("<url>"), std::string::npos);
  }

  TEST(ALabel, IsWhatTheArgumentsSectionCallsAPositionalSlot)
  {
    auto const screen{ oxbox::cli::FormatHelp<Painter>("painter") };

    EXPECT_NE(screen.find("Arguments:"), std::string::npos);

    // "page" alone would be satisfied by the description beside it, so the
    // assertion is the left column: two spaces in from a line start
    EXPECT_NE(screen.find("\n  page "), std::string::npos);
    EXPECT_EQ(screen.find("\n  url"),   std::string::npos);
    EXPECT_NE(screen.find("the page to paint"), std::string::npos);
  }

  TEST(ALabel, IsWhatAMissingArgumentNamesTheSlotBy)
  {
    Painter app;

    try {
      oxbox::cli::Apply(app, Args{ }, "painter");
      FAIL() << "expected a MissingArgument";
    } catch (oxbox::cli::MissingArgument const& failure) {
      EXPECT_EQ(failure.parameter, "page");
      EXPECT_EQ(failure.description, "the page to paint");
    }
  }

  // ── the collector takes the tail ─────────────────────────────────────

  TEST(TheRestCollector, TakesEveryTokenAfterTheSentinelInOrder)
  {
    auto const app{ Parsed<Wrapper>({ "--headless", "--",
                                      "--disable-gpu", "--user-agent=x",
                                      "positional-looking" }) };

    EXPECT_TRUE(app.headless);
    EXPECT_EQ(app.chromium_switches,
              Strings({ "--disable-gpu", "--user-agent=x",
                        "positional-looking" }));
  }

  TEST(TheRestCollector, TakesTheTokensVerbatim)
  {
    auto const app{ Parsed<Wrapper>({ "--", "a,b", "c\\,d", "--x=1,2" }) };

    EXPECT_EQ(app.chromium_switches, Strings({ "a,b", "c\\,d", "--x=1,2" }));
  }

  TEST(TheRestCollector, DoesNotTakeTheTokensAsPositionals)
  {
    Wrapper app;
    auto const outcome{ oxbox::cli::ParseOptions(
      app, Args{ "the-url", "--", "--disable-gpu" }) };

    EXPECT_EQ(outcome.positionals, Views({ "the-url" }));
    EXPECT_EQ(app.chromium_switches, Strings({ "--disable-gpu" }));
  }

  TEST(TheRestCollector, IsEmptyWhenTheSentinelCarriesNothing)
  {
    // the sentinel is the occurrence: an empty tail is assigned, not defaulted
    EXPECT_TRUE(Parsed<Wrapper>({ "--headless", "--" })
                  .chromium_switches.empty());
  }

  TEST(TheRestCollector, IsUntouchedWhenThereIsNoSentinel)
  {
    auto const app{ Parsed<Wrapper>({ "--headless", "a-word" }) };

    EXPECT_TRUE(app.chromium_switches.empty());
  }

  TEST(TheRestCollector, IsNeverMatchedAsAnOptionOfItsOwn)
  {
    // `--` plus the marker would be `----`, and the marker is not a name
    try {
      Parsed<Wrapper>({ "----" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_EQ(failure.option, "--");
    }
  }

  TEST(TheRestCollector, IsNotInTheSuggestionVocabulary)
  {
    try {
      Parsed<Wrapper>({ "--chromium-switches" });
      FAIL() << "expected an UnknownOption";
    } catch (oxbox::cli::UnknownOption const& failure) {
      EXPECT_TRUE(failure.suggestion.empty());
    }
  }

  TEST(TheRestCollector, TakesNoPartInTheArityBookkeeping)
  {
    auto const app{ Parsed<Wrapper>({ "--", "--", "x" }) };

    EXPECT_EQ(app.chromium_switches, Strings({ "--", "x" }));
  }

  TEST(TheRestCollector, TakesAHelpTokenAfterTheSentinelRatherThanShowingHelp)
  {
    Wrapper app;
    auto const outcome{ oxbox::cli::ParseOptions(
      app, Args{ "--", "--help" }) };

    EXPECT_FALSE(outcome.help_requested);
    EXPECT_EQ(app.chromium_switches, Strings({ "--help" }));
  }

  TEST(TheRestCollector, LeavesAHelpBeforeTheSentinelDecisive)
  {
    Wrapper app;
    auto const outcome{ oxbox::cli::ParseOptions(
      app, Args{ "--help", "--", "--disable-gpu" }) };

    EXPECT_TRUE(outcome.help_requested);
    EXPECT_TRUE(app.chromium_switches.empty());
  }

  // ── without a collector, nothing changed ─────────────────────────────

  TEST(WithoutACollector, TheTailIsStillPositional)
  {
    Plain app;
    auto const outcome{ oxbox::cli::ParseOptions(
      app, Args{ "--headless", "--", "--disable-gpu", "word" }) };

    EXPECT_TRUE(app.headless);
    EXPECT_EQ(outcome.positionals, Views({ "--disable-gpu", "word" }));
    EXPECT_TRUE(outcome.tail.empty());
  }

  TEST(WithoutACollector, TheUsageLineSaysNothingAboutATail)
  {
    auto const screen{ oxbox::cli::FormatHelp<Plain>("plain") };

    EXPECT_EQ(screen.find("[-- ...]"), std::string::npos);
  }

  // ── the three accepted types ─────────────────────────────────────────

  TEST(TheRestCollector, DeliversViewsWhenTheMemberIsSpelledThatWay)
  {
    // `line` is named and not a temporary: the views point into it
    Args const line{ "--quiet", "--", "--disable-gpu", "x" };
    Borrowing app;
    oxbox::cli::ParseOptions(app, line);

    EXPECT_TRUE(app.quiet);
    EXPECT_EQ(app.passthrough, Views({ "--disable-gpu", "x" }));
    EXPECT_EQ(app.passthrough.front().data(), line[2].data());
  }

  TEST(TheRestCollector, JoinsWithSingleSpacesWhenTheMemberIsAString)
  {
    auto const app{ Parsed<Joining>({ "--", "--disable-gpu",
                                      "--user-agent=x" }) };

    EXPECT_EQ(app.command_line, "--disable-gpu --user-agent=x");
  }

  TEST(TheRestCollector, CannotRoundTripAJoinedTokenContainingASpace)
  {
    auto const one { Parsed<Joining>({ "--", "a b" })      };
    auto const two { Parsed<Joining>({ "--", "a", "b" })   };

    EXPECT_EQ(one.command_line, "a b");
    EXPECT_EQ(one.command_line, two.command_line);
  }

  TEST(TheRestCollector, JoinsToNothingWhenTheSentinelCarriesNothing)
  {
    EXPECT_TRUE(Parsed<Joining>({ "--" }).command_line.empty());
  }

  // ── the sentinel belongs to the segment it was written in ────────────

  TEST(TheRestCollector, OnAParentTakesTheTailAndStopsTheDescent)
  {
    // a dispatch word after the sentinel is a token, not a trigger
    Layered app;
    auto const outcome{ oxbox::cli::ParseOptions(
      app, Args{ "--", "--leaf", "--depth", "2" }) };

    EXPECT_FALSE(outcome.Dispatches());
    EXPECT_EQ(app.parent_rest,
              Strings({ "--leaf", "--depth", "2" }));
  }

  TEST(TheRestCollector, BelongsToTheChildWhenWrittenAfterTheTrigger)
  {
    Layered app;

    ASSERT_EQ(oxbox::cli::Apply(app, Args{ "--leaf", "--depth", "3",
                                           "--", "--disable-gpu" },
                                "layered").Status(),
              CliStatus::RAN);

    auto const& leaf{ Command::Get<Leaf>() };
    EXPECT_EQ(leaf.depth, 3);
    EXPECT_EQ(leaf.leaf_rest, Strings({ "--disable-gpu" }));
    EXPECT_TRUE(app.parent_rest.empty());
  }

  TEST(TheHelpScreen, DoesNotOfferAnAncestorsCollectorFromFurtherDownTheLine)
  {
    // a sentinel in the parent's segment would have ended the descent, so
    // this line and that tail are mutually exclusive
    auto const screen{
      oxbox::cli::FormatChainHelp<Leaf, Layered>({ "--leaf" }, "layered") };

    EXPECT_NE(screen.find("-- <leaf-rest>..."),   std::string::npos);
    EXPECT_NE(screen.find("layered options:"),    std::string::npos);
    EXPECT_NE(screen.find("--verbose"),           std::string::npos);
    EXPECT_EQ(screen.find("-- <parent-rest>..."), std::string::npos);
  }

  // ── the two passes agree about the split ─────────────────────────────

  TEST(TheRestCollector, IsJudgedTheSameWayByTheWalkThatWritesNothing)
  {
    // pass one reads the line over types alone, with nothing to write into
    auto const scanned{ oxbox::cli::ScanOptions<Wrapper>(
      Args{ "the-url", "--", "--disable-gpu" }) };

    EXPECT_EQ(scanned.positionals, Views({ "the-url" }));
    EXPECT_EQ(scanned.tail, Views({ "--disable-gpu" }));
  }

  TEST(TheRestCollector, LetsALineThroughThatWouldOtherwiseHaveTooManyWords)
  {
    // Wrapper's operator() takes one optional positional
    Wrapper app;
    auto const result{ oxbox::cli::Apply(
      app, Args{ "the-url", "--", "--a", "--b", "--c" }, "wrapper") };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 0);
    EXPECT_EQ(app.chromium_switches, Strings({ "--a", "--b", "--c" }));
  }

  TEST(WithoutACollector, TheSameLineIsStillRefusedForItsExtraWords)
  {
    // Renamed has no collector and no operator(): the token has nowhere to go
    Renamed app;

    try {
      oxbox::cli::Apply(app, Args{ "--", "--a" }, "renamed");
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "--a");
    }
  }

  // ── the help screen says the line takes a tail ───────────────────────

  TEST(TheHelpScreen, EndsTheUsageLineWithTheSentinelWhenThereIsACollector)
  {
    auto const screen{ oxbox::cli::FormatHelp<Wrapper>("wrapper") };

    EXPECT_NE(screen.find("usage: wrapper [options] [url] [-- ...]"),
              std::string::npos);
  }

  TEST(TheHelpScreen, GivesTheCollectorARowOfItsOwnShape)
  {
    auto const screen{ oxbox::cli::FormatHelp<Wrapper>("wrapper") };

    // the label is the marker here and names nothing, so the row shows the member
    EXPECT_NE(screen.find("-- <chromium-switches>..."), std::string::npos);
    EXPECT_NE(screen.find("handed over untouched"), std::string::npos);
  }

  TEST(TheHelpScreen, DoesNotShowTheCollectorAsAnOption)
  {
    auto const screen{ oxbox::cli::FormatHelp<Wrapper>("wrapper") };

    EXPECT_EQ(screen.find("--chromium-switches"), std::string::npos);

    // a default is rendered on a continuation line of its own, so the row's
    // whole block is sliced out rather than its first line
    auto const row{ screen.find("-- <chromium-switches>...") };
    ASSERT_NE(row, std::string::npos);
    auto const next{ screen.find("\n  -h, --help", row) };
    ASSERT_NE(next, std::string::npos);

    EXPECT_EQ(screen.substr(row, next - row).find("(default:"),
              std::string::npos);
    // a control: this screen does carry a default somewhere
    EXPECT_NE(screen.find("(default: false)"), std::string::npos);
  }

  TEST(TheHelpScreen, ListsTheCollectorEvenWhereItIsTheOnlyMember)
  {
    auto const screen{ oxbox::cli::FormatHelp<Joining>("joining") };

    EXPECT_NE(screen.find("-- <command-line>..."), std::string::npos);
    EXPECT_NE(screen.find("[-- ...]"), std::string::npos);
  }
}
