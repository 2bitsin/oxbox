#include "oxbox/platform/contract.hpp"

#include <gtest/gtest.h>

#include <string_view>

using namespace oxbox;

using platform::ContractMode;
using platform::Contracts;

namespace
{
  constexpr std::string_view NEEDED{ "the count fits the buffer" };
  constexpr std::string_view PROMISED{ "the cursor is at the end" };
  constexpr std::string_view UNWRITTEN{ "the big-endian road" };

  using Ignoring = Contracts<ContractMode::IGNORE>;
  using Complaining = Contracts<ContractMode::COMPLAIN>;
  using Stopping = Contracts<ContractMode::STOP>;
}

static_assert((Ignoring::Expects(false, NEEDED), true));
static_assert((Ignoring::Ensures(false, PROMISED), true));
static_assert((Ignoring::NotImplemented(UNWRITTEN), true));
static_assert((Complaining::Expects(true, NEEDED), true));
static_assert((Complaining::Ensures(true, PROMISED), true));
static_assert((Stopping::Expects(true, NEEDED), true));
static_assert((Stopping::Ensures(true, PROMISED), true));

TEST(PlatformContract, HeldConditionsPassSilently)
{
  testing::internal::CaptureStderr();
  Stopping::Expects(true, NEEDED);
  Stopping::Ensures(true, PROMISED);
  Complaining::Expects(true, NEEDED);
  Complaining::Ensures(true, PROMISED);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
}

TEST(PlatformContract, IgnoringSwallowsAFailedPrecondition)
{
  testing::internal::CaptureStderr();
  Ignoring::Expects(false, NEEDED);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
}

TEST(PlatformContract, IgnoringSwallowsAFailedPostcondition)
{
  testing::internal::CaptureStderr();
  Ignoring::Ensures(false, PROMISED);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
}

TEST(PlatformContract, IgnoringReturnsFromAnUnimplementedPath)
{
  testing::internal::CaptureStderr();
  Ignoring::NotImplemented(UNWRITTEN);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
}

// Reaching std::unreachable() is undefined, so the ignored road is proved by
// instantiating it rather than by running it.
TEST(PlatformContract, IgnoringLeavesAnUnreachableValueToTheCompiler)
{
  auto const road{ &Ignoring::Unreachable<int> };
  EXPECT_TRUE(road != nullptr);
}
