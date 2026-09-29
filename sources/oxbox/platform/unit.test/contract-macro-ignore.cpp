// OXBOX_EXPECTS and OXBOX_ENSURES under OFF: the condition is not in the
// build. NeverDefined has no definition anywhere; this executable links.
#define OXBOX_CONTRACT_MODE ::oxbox::platform::ContractMode::OFF
#define OXBOX_CONTRACTS_IGNORE
#include "oxbox/platform/contract.hpp"

#include "oxbox/platform/unit.test/contract-fixtures.hpp"

#include <gtest/gtest.h>

#include <cstddef>

// External linkage on purpose: an internal one declared and never defined
// is -Wunused-function on gcc and clang whether or not it is named.
namespace oxbox::platform::unit_test::detail::contract_macro_ignore
{
  auto NeverDefined() -> bool;
}

using namespace oxbox;

using platform::unit_test::Counted;
using platform::unit_test::NEEDED;
using platform::unit_test::PROMISED;
using platform::unit_test::StderrOf;
using platform::unit_test::detail::contract_macro_ignore::NeverDefined;

static_assert((OXBOX_EXPECTS(false, NEEDED), true));
static_assert((OXBOX_ENSURES(false, PROMISED), true));

TEST(PlatformContractMacroIgnore, AConditionIsNotEvaluated)
{
  auto evaluations{ std::size_t{ } };
  OXBOX_EXPECTS(Counted(evaluations, false), NEEDED);
  OXBOX_ENSURES(Counted(evaluations, false), PROMISED);
  EXPECT_EQ(evaluations, std::size_t{ 0 });
}

TEST(PlatformContractMacroIgnore, TheConditionIsNotLinked)
{
  OXBOX_EXPECTS(NeverDefined(), NEEDED);
  OXBOX_ENSURES(NeverDefined(), PROMISED);
  SUCCEED();
}

TEST(PlatformContractMacroIgnore, NothingIsSaid)
{
  EXPECT_EQ(StderrOf([] {
    OXBOX_EXPECTS(false, NEEDED);
    OXBOX_ENSURES(false, PROMISED);
  }), "");
}
