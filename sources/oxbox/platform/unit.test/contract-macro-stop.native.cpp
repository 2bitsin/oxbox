// OXBOX_EXPECTS and OXBOX_ENSURES under STOP: a held condition runs once, a
// broken one ends the program saying the call site.
#define OXBOX_CONTRACT_MODE ::oxbox::platform::ContractMode::STOP
#include "oxbox/platform/contract.hpp"

#include "oxbox/platform/unit.test/contract-fixtures.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <source_location>

using namespace oxbox;

using platform::unit_test::Counted;
using platform::unit_test::NEEDED;
using platform::unit_test::POSTCONDITION;
using platform::unit_test::PRECONDITION;
using platform::unit_test::PROMISED;
using platform::unit_test::SaidAt;

using PlatformContractMacroDeath = platform::unit_test::DeathTest;

TEST_F(PlatformContractMacroDeath, AHeldConditionIsEvaluatedOnce)
{
  auto evaluations{ std::size_t{ } };
  OXBOX_EXPECTS(Counted(evaluations), NEEDED);
  OXBOX_ENSURES(Counted(evaluations), PROMISED);
  EXPECT_EQ(evaluations, std::size_t{ 2 });
}

TEST_F(PlatformContractMacroDeath, ABrokenConditionEndsTheProgramAtItsSite)
{
  auto const above  { std::source_location::current() };
  auto const expects{ [] { OXBOX_EXPECTS(false, NEEDED);   } };
  auto const ensures{ [] { OXBOX_ENSURES(false, PROMISED); } };
  EXPECT_DEATH(expects(), SaidAt(PRECONDITION,  above, 1));
  EXPECT_DEATH(ensures(), SaidAt(POSTCONDITION, above, 2));
}
