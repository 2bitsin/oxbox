#include "oxbox/platform/contract.hpp"

#include <gtest/gtest.h>

#include <format>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace oxbox;

using platform::ContractMode;
using platform::Contracts;

namespace
{
  constexpr std::string_view NEEDED{ "the count fits the buffer" };
  constexpr std::string_view PROMISED{ "the cursor is at the end" };
  constexpr std::string_view UNWRITTEN{ "the big-endian road" };

  using Ignoring    = Contracts<ContractMode::IGNORE>;
  using Complaining = Contracts<ContractMode::COMPLAIN>;
  using Stopping    = Contracts<ContractMode::STOP>;
  using Throwing    = Contracts<ContractMode::THROW>;

  auto FailureLine(std::string_view kind, std::source_location where,
                   std::string_view text) -> std::string
  {
    return std::format("{}: {}:{} {}: {}", kind, where.file_name(),
                       where.line(), where.function_name(), text);
  }

  // The line is said before it is thrown, so the road it is said on is
  // swallowed here and read back by the native test instead.
  auto WhatOf(auto&& broken) -> std::string
  {
    testing::internal::CaptureStderr();
    auto said{ std::string{ } };
    try { broken(); }
    catch (platform::ContractFailure const& failure) { said = failure.what(); }
    static_cast<void>(testing::internal::GetCapturedStderr());
    return said;
  }
}

static_assert((Ignoring::Expects(false, NEEDED), true));
static_assert((Ignoring::Ensures(false, PROMISED), true));
static_assert((Ignoring::NotImplemented(UNWRITTEN), true));
static_assert((Complaining::Expects(true, NEEDED), true));
static_assert((Complaining::Ensures(true, PROMISED), true));
static_assert((Stopping::Expects(true, NEEDED), true));
static_assert((Stopping::Ensures(true, PROMISED), true));
static_assert((Throwing::Expects(true, NEEDED), true));
static_assert((Throwing::Ensures(true, PROMISED), true));

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

TEST(PlatformContractThrow, AFailedPreconditionThrowsTheLineItSays)
{
  auto const where{ std::source_location::current() };
  EXPECT_EQ(WhatOf([&] { Throwing::Expects(false, NEEDED, where); }),
            FailureLine("precondition", where, NEEDED));
}

TEST(PlatformContractThrow, AFailedPostconditionThrowsTheLineItSays)
{
  auto const where{ std::source_location::current() };
  EXPECT_EQ(WhatOf([&] { Throwing::Ensures(false, PROMISED, where); }),
            FailureLine("postcondition", where, PROMISED));
}

TEST(PlatformContractThrow, AnUnreachableValueThrowsTheLineItSays)
{
  auto const where{ std::source_location::current() };
  EXPECT_EQ(WhatOf([&] { Throwing::Unreachable(7, where); }),
            FailureLine("unreachable", where, "7"));
}

TEST(PlatformContractThrow, AnUnimplementedPathThrowsTheLineItSays)
{
  auto const where{ std::source_location::current() };
  EXPECT_EQ(WhatOf([&] { Throwing::NotImplemented(UNWRITTEN, where); }),
            FailureLine("not implemented", where, UNWRITTEN));
}

TEST(PlatformContractThrow, TheFailureCarriesTheKindTheTextAndThePlace)
{
  auto const where{ std::source_location::current() };
  testing::internal::CaptureStderr();
  try
  {
    Throwing::Expects(false, NEEDED, where);
    static_cast<void>(testing::internal::GetCapturedStderr());
    FAIL() << "a broken precondition did not throw";
  }
  catch (platform::ContractFailure const& failure)
  {
    static_cast<void>(testing::internal::GetCapturedStderr());
    EXPECT_EQ(failure.Kind(), "precondition");
    EXPECT_EQ(failure.Text(), NEEDED);
    EXPECT_STREQ(failure.Where().file_name(), where.file_name());
    EXPECT_EQ(failure.Where().line(), where.line());
  }
}

TEST(PlatformContractThrow, AFailureIsCaughtAsARuntimeError)
{
  testing::internal::CaptureStderr();
  EXPECT_THROW(Throwing::NotImplemented(UNWRITTEN), std::runtime_error);
  static_cast<void>(testing::internal::GetCapturedStderr());
}

TEST(PlatformContractThrow, HeldConditionsThrowNothing)
{
  testing::internal::CaptureStderr();
  EXPECT_NO_THROW(Throwing::Expects(true, NEEDED));
  EXPECT_NO_THROW(Throwing::Ensures(true, PROMISED));
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
}
