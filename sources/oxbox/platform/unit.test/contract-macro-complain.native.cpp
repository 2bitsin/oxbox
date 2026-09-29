// OXBOX_EXPECTS and OXBOX_ENSURES under COMPLAIN: the condition runs once,
// the line said on stderr names the call site, and the caller continues.
#define OXBOX_CONTRACT_MODE ::oxbox::platform::ContractMode::COMPLAIN
#include "oxbox/platform/contract.hpp"

#include "oxbox/platform/unit.test/contract-fixtures.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <source_location>

using namespace oxbox;

using platform::unit_test::Counted;
using platform::unit_test::ExpectSaidOnce;
using platform::unit_test::NEEDED;
using platform::unit_test::POSTCONDITION;
using platform::unit_test::PRECONDITION;
using platform::unit_test::PROMISED;
using platform::unit_test::SaidAt;
using platform::unit_test::StderrOf;

TEST(PlatformContractMacroComplain, AHeldConditionRunsOnceAndIsSilent)
{
  auto       evaluations{ std::size_t{ } };
  auto const said       { StderrOf([&] {
    OXBOX_EXPECTS(Counted(evaluations), NEEDED);
    OXBOX_ENSURES(Counted(evaluations), PROMISED);
  }) };
  EXPECT_EQ(said, "");
  EXPECT_EQ(evaluations, std::size_t{ 2 });
}

TEST(PlatformContractMacroComplain, ABrokenConditionSaysItsSiteAndReturns)
{
  auto const above  { std::source_location::current() };
  auto const expects{ [](std::size_t& evaluations) { OXBOX_EXPECTS(Counted(evaluations, false), NEEDED);   } };
  auto const ensures{ [](std::size_t& evaluations) { OXBOX_ENSURES(Counted(evaluations, false), PROMISED); } };
  ExpectSaidOnce(StderrOf, expects, SaidAt(PRECONDITION,  above, 1));
  ExpectSaidOnce(StderrOf, ensures, SaidAt(POSTCONDITION, above, 2));
}
