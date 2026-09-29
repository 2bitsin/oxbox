#pragma once
// What the contract suites share: the texts, the line a failure says, a
// condition that counts its evaluations, the roads a said line is read on.

#include "oxbox/platform/contract.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace oxbox::platform::unit_test::detail::contract_fixtures
{
  constexpr std::string_view NEEDED   { "the count fits the buffer" };
  constexpr std::string_view PROMISED { "the cursor is at the end" };
  constexpr std::string_view UNWRITTEN{ "the big-endian road" };

  struct ContractKind
  {
    std::string_view name;
    std::string_view text;
  };

  constexpr ContractKind PRECONDITION { "precondition",  NEEDED   };
  constexpr ContractKind POSTCONDITION{ "postcondition", PROMISED };

  // Spelled here on its own, so the format is pinned and not read back.
  inline auto FailureLine(std::string_view kind, std::source_location where,
                          std::string_view text) -> std::string
  {
    return std::format("{}: {}:{} {}: {}", kind, where.file_name(),
                       where.line(), where.function_name(), text);
  }

  inline auto SaidLine(std::string_view kind, std::source_location where,
                       std::string_view text) -> std::string
  {
    return FailureLine(kind, where, text) + "\n";
  }

  // What a macro said from a test body `lines_below` a place, where the
  // function name is gtest's and only the file and line are the site's.
  inline auto SaidAt(ContractKind kind, std::source_location above,
                     std::uint32_t lines_below)
    -> testing::Matcher<std::string const&>
  {
    using testing::AllOf;
    using testing::HasSubstr;
    return AllOf(HasSubstr(std::format("{}: {}:{} ", kind.name,
                                       above.file_name(),
                                       above.line() + lines_below)),
                 HasSubstr("TestBody"), HasSubstr(kind.text));
  }

  inline auto Counted(std::size_t& evaluations, bool held = true) -> bool
  {
    ++evaluations;
    return held;
  }

  // The line is said on stderr before it is thrown; the failure object is
  // what a test reads, so the said line is swallowed here.
  auto Caught(auto&& broken) -> std::optional<ContractFailure>
  {
    testing::internal::CaptureStderr();
    auto caught{ std::optional<ContractFailure>{ } };
    try { std::forward<decltype(broken)>(broken)(); }
    catch (ContractFailure const& failure) { caught = failure; }
    static_cast<void>(testing::internal::GetCapturedStderr());
    return caught;
  }

  inline constexpr auto WhatOf{ [](auto&& broken) -> std::string {
    auto const caught{ Caught(std::forward<decltype(broken)>(broken)) };
    return caught ? caught->what() : std::string{ };
  } };

  inline constexpr auto StderrOf{ [](auto&& run) -> std::string {
    testing::internal::CaptureStderr();
    std::forward<decltype(run)>(run)();
    return testing::internal::GetCapturedStderr();
  } };

  // A broken site run once on a road, which answers the line it said.
  auto ExpectSaidOnce(auto const& road, auto const& broken,
                      testing::Matcher<std::string const&> const& said_at)
    -> void
  {
    auto       evaluations{ std::size_t{ } };
    auto const said       { road([&] { broken(evaluations); }) };
    EXPECT_EQ(evaluations, std::size_t{ 1 });
    EXPECT_THAT(said, said_at);
  }

  class DeathTest : public testing::Test
  {
  protected:
    static auto SetUpTestSuite() -> void
    { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
  };
}

namespace oxbox::platform::unit_test
{
  using detail::contract_fixtures::Caught;
  using detail::contract_fixtures::ContractKind;
  using detail::contract_fixtures::Counted;
  using detail::contract_fixtures::DeathTest;
  using detail::contract_fixtures::ExpectSaidOnce;
  using detail::contract_fixtures::FailureLine;
  using detail::contract_fixtures::NEEDED;
  using detail::contract_fixtures::POSTCONDITION;
  using detail::contract_fixtures::PRECONDITION;
  using detail::contract_fixtures::PROMISED;
  using detail::contract_fixtures::SaidAt;
  using detail::contract_fixtures::SaidLine;
  using detail::contract_fixtures::StderrOf;
  using detail::contract_fixtures::UNWRITTEN;
  using detail::contract_fixtures::WhatOf;
}
