// Deaths and reports read back are native: GTEST_HAS_DEATH_TEST is 0 in the
// browser lane, and there the line goes to the console rather than to stderr.

#include "oxbox/platform/contract.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <typeinfo>

using namespace oxbox;

using platform::ContractMode;
using platform::Contracts;
using testing::AllOf;
using testing::HasSubstr;

namespace
{
  constexpr std::string_view NEEDED{ "the count fits the buffer" };
  constexpr std::string_view PROMISED{ "the cursor is at the end" };
  constexpr std::string_view UNWRITTEN{ "the big-endian road" };

  using Complaining = Contracts<ContractMode::COMPLAIN>;
  using Stopping    = Contracts<ContractMode::STOP>;
  using Throwing    = Contracts<ContractMode::THROW>;

  struct NoFormatter { int value{ }; };

  auto FailureLine(std::string_view kind, std::source_location where,
                   std::string_view text) -> std::string
  {
    return std::format("{}: {}:{} {}: {}", kind, where.file_name(),
                       where.line(), where.function_name(), text);
  }

  auto SaidLine(std::string_view kind, std::source_location where,
                std::string_view text) -> std::string
  {
    return FailureLine(kind, where, text) + "\n";
  }
}

class PlatformContractDeath : public testing::Test
{
protected:
  static auto SetUpTestSuite() -> void
  { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
};

TEST_F(PlatformContractDeath, AFailedPreconditionSaysWhereAndWhat)
{
  auto const where{ std::source_location::current() };
  EXPECT_DEATH(Stopping::Expects(false, NEEDED, where),
               HasSubstr(FailureLine("precondition", where, NEEDED)));
}

TEST_F(PlatformContractDeath, AFailedPostconditionSaysWhereAndWhat)
{
  auto const where{ std::source_location::current() };
  EXPECT_DEATH(Stopping::Ensures(false, PROMISED, where),
               HasSubstr(FailureLine("postcondition", where, PROMISED)));
}

TEST_F(PlatformContractDeath, AnUnreachableValueIsSaidWhereFormatKnowsIt)
{
  auto const where{ std::source_location::current() };
  EXPECT_DEATH(Stopping::Unreachable(7, where),
               HasSubstr(FailureLine("unreachable", where, "7")));
}

TEST_F(PlatformContractDeath, AnUnreachableValueFallsBackToItsTypeName)
{
  auto const where{ std::source_location::current() };
  EXPECT_DEATH(Stopping::Unreachable(NoFormatter{ 7 }, where),
               HasSubstr(FailureLine("unreachable", where,
                                     typeid(NoFormatter).name())));
}

TEST_F(PlatformContractDeath, AnUnimplementedPathFails)
{
  auto const where{ std::source_location::current() };
  EXPECT_DEATH(Stopping::NotImplemented(UNWRITTEN, where),
               HasSubstr(FailureLine("not implemented", where, UNWRITTEN)));
}

TEST_F(PlatformContractDeath, AComplainedUnreachableValueStillEndsTheProgram)
{
  auto const where{ std::source_location::current() };
  EXPECT_DEATH(Complaining::Unreachable(7, where),
               HasSubstr(FailureLine("unreachable", where, "7")));
}

TEST_F(PlatformContractDeath, TheLocationDefaultsToTheCallSite)
{
  EXPECT_DEATH(Stopping::Expects(false, NEEDED),
               AllOf(HasSubstr("platform/unit.test/contract.native.cpp:"),
                     HasSubstr("TestBody"), HasSubstr(NEEDED)));
}

TEST(PlatformContractReport, AComplainedPreconditionSaysItsLineAndReturns)
{
  auto const where{ std::source_location::current() };
  testing::internal::CaptureStderr();
  Complaining::Expects(false, NEEDED, where);
  EXPECT_EQ(testing::internal::GetCapturedStderr(),
            SaidLine("precondition", where, NEEDED));
}

TEST(PlatformContractReport, AComplainedPostconditionSaysItsLineAndReturns)
{
  auto const where{ std::source_location::current() };
  testing::internal::CaptureStderr();
  Complaining::Ensures(false, PROMISED, where);
  EXPECT_EQ(testing::internal::GetCapturedStderr(),
            SaidLine("postcondition", where, PROMISED));
}

TEST(PlatformContractReport, AComplainedUnimplementedPathSaysItsLineAndReturns)
{
  auto const where{ std::source_location::current() };
  testing::internal::CaptureStderr();
  Complaining::NotImplemented(UNWRITTEN, where);
  EXPECT_EQ(testing::internal::GetCapturedStderr(),
            SaidLine("not implemented", where, UNWRITTEN));
}

TEST(PlatformContractReport, AComplainedSiteSaysItsLineEveryTimeItIsHit)
{
  auto const where{ std::source_location::current() };
  testing::internal::CaptureStderr();
  Complaining::Expects(false, NEEDED, where);
  Complaining::Expects(false, NEEDED, where);
  EXPECT_EQ(testing::internal::GetCapturedStderr(),
            SaidLine("precondition", where, NEEDED)
              + SaidLine("precondition", where, NEEDED));
}

TEST(PlatformContractReport, AThrownFailureSaysItsLineBeforeItIsThrown)
{
  auto const where{ std::source_location::current() };
  testing::internal::CaptureStderr();
  EXPECT_THROW(Throwing::Expects(false, NEEDED, where),
               platform::ContractFailure);
  EXPECT_EQ(testing::internal::GetCapturedStderr(),
            SaidLine("precondition", where, NEEDED));
}

TEST(PlatformContractReport, AThrownUnreachableValueSaysItsLineToo)
{
  auto const where{ std::source_location::current() };
  testing::internal::CaptureStderr();
  EXPECT_THROW(Throwing::Unreachable(7, where), platform::ContractFailure);
  EXPECT_EQ(testing::internal::GetCapturedStderr(),
            SaidLine("unreachable", where, "7"));
}
