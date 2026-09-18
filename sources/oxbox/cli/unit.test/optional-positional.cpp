// A positional the line may leave out: std::optional<T> as an operator()
// parameter. Binding is one greedy left-to-right pass with no lookahead
// and no giving a token back. An optional option is parse.cpp's.

#include "oxbox/cli/help.hpp"
#include "oxbox/cli/invoke.hpp"
#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/optional-positional-cli.inc"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <tuple>
#include <string_view>
#include <string>
#include <vector>

namespace
{
  using oxbox::cli::unit_test::Find;
  using oxbox::cli::unit_test::Greedy;
  using oxbox::cli::unit_test::Seen;
  using oxbox::cli::unit_test::Owning;
  using oxbox::cli::unit_test::Several;
  using oxbox::cli::unit_test::Slots;
  using oxbox::cli::unit_test::Viewing;
  using oxbox::cli::unit_test::Shaped;
  using Args    = std::vector<std::string_view>;
  using Strings = std::vector<std::string>;

  // ── the headline: filled one by one while there are tokens ───────────

  TEST(OptionalPositionals, AnOptionalLeftOutIsNulloptAndConsumesNothing)
  {
    Seen seen;
    Find find{ &seen };

    oxbox::cli::Apply(find, Args{ "foo" });

    EXPECT_EQ(seen.query, "foo");
    EXPECT_FALSE(seen.limit.has_value());
  }

  TEST(OptionalPositionals, AnOptionalGivenATokenHoldsIt)
  {
    Seen seen;
    Find find{ &seen };

    oxbox::cli::Apply(find, Args{ "foo", "5" });

    EXPECT_EQ(seen.query, "foo");
    ASSERT_TRUE(seen.limit.has_value());
    EXPECT_EQ(*seen.limit, 5);
  }

  TEST(OptionalPositionals, AStringOfOptionalsIsFilledOneByOne)
  {
    auto const bound = [](Args const& line) {
      Seen    seen;
      Several several{ &seen };
      oxbox::cli::Apply(several, line);
      return seen;
    };

    auto const none{ bound({ "foo" }) };
    EXPECT_FALSE(none.limit.has_value());
    EXPECT_FALSE(none.extra.has_value());

    auto const one{ bound({ "foo", "5" }) };
    ASSERT_TRUE(one.limit.has_value());
    EXPECT_EQ(*one.limit, 5);
    EXPECT_FALSE(one.extra.has_value());

    auto const both{ bound({ "foo", "5", "more" }) };
    ASSERT_TRUE(both.limit.has_value());
    EXPECT_EQ(*both.limit, 5);
    ASSERT_TRUE(both.extra.has_value());
    EXPECT_EQ(*both.extra, "more");
  }

  TEST(OptionalPositionals, ATokenWithNothingLeftToAbsorbItIsStillTooMany)
  {
    Seen seen;
    Find find{ &seen };

    try {
      oxbox::cli::Apply(find, Args{ "foo", "5", "spare" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "spare");
    }
  }

  TEST(OptionalPositionals, AnExhaustedOptionalIsNotAMissingArgument)
  {
    // MissingArgument is for required parameters, and an optional that
    // ran out is the answer rather than the failure
    Seen seen;
    Find find{ &seen };

    EXPECT_NO_THROW(oxbox::cli::Apply(find, Args{ "foo" }));
  }

  TEST(OptionalPositionals, ARequiredParameterStillRunsOutByName)
  {
    Seen seen;
    Find find{ &seen };

    try {
      oxbox::cli::Apply(find, Args{ });
      FAIL() << "expected a MissingArgument";
    } catch (oxbox::cli::MissingArgument const& failure) {
      EXPECT_EQ(failure.parameter, "query");
    }
  }

  // ── greedy against a tail ────────────────────────────────────────────

  TEST(OptionalPositionals, ASurplusTokenGoesToTheOptionalBeforeTheTail)
  {
    Seen   seen;
    Greedy greedy{ &seen };

    oxbox::cli::Apply(greedy, Args{ "a", "5", "c", "d" });

    EXPECT_EQ(seen.query, "a");
    ASSERT_TRUE(seen.limit.has_value());
    EXPECT_EQ(*seen.limit, 5);
    EXPECT_EQ(seen.rest, Strings({ "c", "d" }));
  }

  TEST(OptionalPositionals, WithNothingSurplusTheOptionalAndTheTailAreBothEmpty)
  {
    Seen   seen;
    Greedy greedy{ &seen };

    oxbox::cli::Apply(greedy, Args{ "a" });

    EXPECT_EQ(seen.query, "a");
    EXPECT_FALSE(seen.limit.has_value());
    EXPECT_TRUE(seen.rest.empty());
  }

  TEST(OptionalPositionals, AnEngagedOptionalThatWillNotConvertRefusesTheLine)
  {
    // decided by counting, not by trying: `limit` engaged because a token
    // was there, so a non-int is a TypeMismatch and not a demotion
    Seen   seen;
    Greedy greedy{ &seen };

    try {
      oxbox::cli::Apply(greedy, Args{ "a", "not-a-number", "c", "d" });
      FAIL() << "expected a TypeMismatch";
    } catch (oxbox::utilities::TypeMismatch const& failure) {
      EXPECT_NE(std::string_view{ failure.what() }.find("limit"),
                std::string_view::npos);
    }
  }

  // ── an array of optionals fills a prefix ─────────────────────────────

  TEST(OptionalPositionals, AnArrayOfOptionalsFillsAPrefixOneByOne)
  {
    auto const bound = [](Args const& line) {
      Seen  seen;
      Slots slots{ &seen };
      oxbox::cli::Apply(slots, line);
      return seen.slots;
    };

    auto const none{ bound({ "foo" }) };
    EXPECT_FALSE(none[0].has_value());
    EXPECT_FALSE(none[1].has_value());
    EXPECT_FALSE(none[2].has_value());

    auto const some{ bound({ "foo", "1", "2" }) };
    ASSERT_TRUE(some[0].has_value());
    ASSERT_TRUE(some[1].has_value());
    EXPECT_EQ(*some[0], 1);
    EXPECT_EQ(*some[1], 2);
    EXPECT_FALSE(some[2].has_value());

    auto const full{ bound({ "foo", "1", "2", "3" }) };
    ASSERT_TRUE(full[2].has_value());
    EXPECT_EQ(*full[2], 3);
  }

  TEST(OptionalPositionals, AnArrayOfOptionalsIsStillBoundedAtItsExtent)
  {
    Seen  seen;
    Slots slots{ &seen };

    try {
      oxbox::cli::Apply(slots, Args{ "foo", "1", "2", "3", "4" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "4");
    }
  }

  // ── an owning variadic positional ────────────────────────────────────

  TEST(OptionalPositionals, AContainerParameterTakesEverythingLeftAndOwnsIt)
  {
    Seen   seen;
    Owning owning{ &seen };

    oxbox::cli::Apply(owning, Args{ "foo", "a", "b", "c" });

    EXPECT_EQ(seen.query, "foo");
    EXPECT_EQ(seen.owned, Strings({ "a", "b", "c" }));
  }

  TEST(OptionalPositionals, AContainerParameterIsHappyWithNothingAtAll)
  {
    Seen   seen;
    Owning owning{ &seen };

    oxbox::cli::Apply(owning, Args{ "foo" });

    EXPECT_EQ(seen.query, "foo");
    EXPECT_TRUE(seen.owned.empty());
  }

  TEST(OptionalPositionals, AContainerOfViewsPointsAtTheCallersOwnTokens)
  {
    // the elements must be views of the line and not of some temporary
    // the conversion made on the way past
    Seen    seen;
    Viewing viewing{ &seen };
    Args const line{ "foo", "a", "b" };

    oxbox::cli::Apply(viewing, line);

    EXPECT_EQ(seen.owned, Strings({ "a", "b" }));
  }

  // ── a shaped optional is all or nothing ──────────────────────────────

  TEST(OptionalPositionals, AShapedOptionalTakesItsWholeWorthOrNoneOfIt)
  {
    auto const bound = [](Args const& line) {
      Seen   seen;
      Shaped shaped{ &seen };
      oxbox::cli::Apply(shaped, line);
      return seen;
    };

    auto const none{ bound({ "foo" }) };
    EXPECT_FALSE(none.pair.has_value());

    auto const both{ bound({ "foo", "1", "2" }) };
    ASSERT_TRUE(both.pair.has_value());
    EXPECT_EQ(*both.pair, (std::array<int, 2>{ 1, 2 }));
  }

  TEST(OptionalPositionals, HalfAShapedOptionalIsNoneOfItAndThenTooMany)
  {
    // one token cannot fill a pair, so the pair yields nullopt and takes
    // nothing -- which leaves that token with nowhere to go
    Seen   seen;
    Shaped shaped{ &seen };

    try {
      oxbox::cli::Apply(shaped, Args{ "foo", "1" });
      FAIL() << "expected an UnexpectedArgument";
    } catch (oxbox::cli::UnexpectedArgument const& failure) {
      EXPECT_EQ(failure.argument, "1");
    }
  }

  // ── the usage line says which is which ───────────────────────────────

  TEST(OptionalPositionals, TheUsageLineBracketsAnOptionalAndAnglesARequired)
  {
    auto const screen{ oxbox::cli::FormatHelp<Find>("find") };

    EXPECT_NE(screen.find("<query>"), std::string::npos);
    EXPECT_NE(screen.find("[limit]"), std::string::npos);
  }

  TEST(OptionalPositionals, TheTailKeepsItsOwnEllipsisSpelling)
  {
    auto const screen{ oxbox::cli::FormatHelp<Greedy>("greedy") };

    EXPECT_NE(screen.find("<query>"), std::string::npos);
    EXPECT_NE(screen.find("[limit]"), std::string::npos);
    EXPECT_NE(screen.find("[rest...]"), std::string::npos);
  }

  // ── the signature rule, pinned where it is decided ───────────────────
  //
  // "once an optional parameter is hit, the rest must also be optional" is
  // a static_assert inside Invoke, so a command that breaks it cannot be
  // written as a fixture -- only the predicate it fires on can be pinned.
  // It forbids `(optional<int> limit, std::string query)`, where one token
  // would go to `limit` and leave `query` unfillable.

  using oxbox::cli::OptionalsComeLast;

  // required first, optional after: the shape everything here declares
  static_assert(OptionalsComeLast<
    std::tuple<std::string, std::optional<int>>>());
  static_assert(OptionalsComeLast<
    std::tuple<std::string, std::optional<int>, std::optional<std::string>>>());
  static_assert(OptionalsComeLast<
    std::tuple<std::string, std::optional<int>,
               oxbox::cli::RangeView<std::string>>>());
  static_assert(OptionalsComeLast<
    std::tuple<std::string, std::array<std::optional<int>, 3>>>());
  static_assert(OptionalsComeLast<std::tuple<>>());
  static_assert(OptionalsComeLast<std::tuple<std::string, std::array<int, 2>>>());

  // a zero-minimum parameter after an unbounded one is legal and always
  // empty
  static_assert(OptionalsComeLast<
    std::tuple<std::vector<std::string>, std::optional<int>>>());

  // and the refusals
  static_assert(!OptionalsComeLast<
    std::tuple<std::optional<int>, std::string>>());
  static_assert(!OptionalsComeLast<
    std::tuple<oxbox::cli::RangeView<std::string>, std::string>>());
  static_assert(!OptionalsComeLast<
    std::tuple<std::vector<std::string>, std::string>>());
  static_assert(!OptionalsComeLast<
    std::tuple<std::string, std::optional<int>, std::string>>());
  static_assert(!OptionalsComeLast<
    std::tuple<std::array<std::optional<int>, 3>, std::string>>());
}
