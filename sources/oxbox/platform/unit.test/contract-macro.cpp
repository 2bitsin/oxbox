// OXBOX_EXPECTS and OXBOX_ENSURES under THROW: the condition runs once, the
// failure carries the call site, a template comma and an optional pass.
#define OXBOX_CONTRACT_MODE ::oxbox::platform::ContractMode::THROW
#include "oxbox/platform/contract.hpp"

#include "oxbox/platform/unit.test/contract-fixtures.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <source_location>

using namespace oxbox;

using platform::unit_test::Caught;
using platform::unit_test::Counted;
using platform::unit_test::ExpectSaidOnce;
using platform::unit_test::NEEDED;
using platform::unit_test::POSTCONDITION;
using platform::unit_test::PRECONDITION;
using platform::unit_test::PROMISED;
using platform::unit_test::SaidAt;
using platform::unit_test::WhatOf;

static_assert((OXBOX_EXPECTS(true, NEEDED), true));
static_assert((OXBOX_ENSURES(true, PROMISED), true));
static_assert((OXBOX_EXPECTS(std::optional<int>{ 7 }, NEEDED), true));

TEST(PlatformContractMacro, AHeldConditionIsEvaluatedOnce)
{
  auto evaluations{ std::size_t{ } };
  OXBOX_EXPECTS(Counted(evaluations), NEEDED);
  OXBOX_ENSURES(Counted(evaluations), PROMISED);
  EXPECT_EQ(evaluations, std::size_t{ 2 });
}

TEST(PlatformContractMacro, ABrokenConditionIsEvaluatedOnceAndThrownAtItsSite)
{
  auto const above  { std::source_location::current() };
  auto const expects{ [](std::size_t& evaluations) { OXBOX_EXPECTS(Counted(evaluations, false), NEEDED);   } };
  auto const ensures{ [](std::size_t& evaluations) { OXBOX_ENSURES(Counted(evaluations, false), PROMISED); } };
  ExpectSaidOnce(WhatOf, expects, SaidAt(PRECONDITION,  above, 1));
  ExpectSaidOnce(WhatOf, ensures, SaidAt(POSTCONDITION, above, 2));
}

TEST(PlatformContractMacro, ATemplateCommaStaysInsideTheCondition)
{
  auto const pair{ std::array<int, 2>{ 1, 2 } };
  EXPECT_NO_THROW(OXBOX_EXPECTS(std::array<int, 2>{ 1, 2 } == pair, NEEDED));
  EXPECT_NO_THROW(OXBOX_ENSURES(std::array<int, 2>{ 1, 2 } == pair, PROMISED));
}

TEST(PlatformContractMacro, AnOptionalIsHeldWhenItHasAValue)
{
  auto const present{ std::optional<int>{ 7 } };
  EXPECT_NO_THROW(OXBOX_EXPECTS(present, NEEDED));
  EXPECT_NO_THROW(OXBOX_ENSURES(present, PROMISED));
}

TEST(PlatformContractMacro, AnEmptyOptionalIsABrokenCondition)
{
  auto const absent{ std::optional<int>{ } };
  auto const caught{ Caught([&] { OXBOX_EXPECTS(absent, NEEDED); }) };
  ASSERT_TRUE(caught.has_value());
  EXPECT_EQ(caught->Text(), NEEDED);
}

TEST(PlatformContractMacro, AnOptionalOfFalseIsHeld)
{
  auto const present{ std::optional<bool>{ false } };
  EXPECT_NO_THROW(OXBOX_EXPECTS(present, NEEDED));
}
