// What an operator() answers is what the run answers: void is a silent
// success, and an int is the code the run exits with.

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/dispatch-cli.inc"

#include <gtest/gtest.h>

#include <string_view>
#include <string>
#include <vector>

namespace
{
  using oxbox::cli::CliStatus;
  using oxbox::cli::unit_test::Numeric;
  using oxbox::cli::unit_test::Silent;
  using oxbox::cli::unit_test::Trace;
  using Args    = std::vector<std::string_view>;

  // ── what operator() returns is what the run returns ───────────────────

  TEST(Results, VoidMeansItRanAndSucceeded)
  {
    Trace  trace;
    Silent silent{ &trace };

    auto const result{ oxbox::cli::Apply(silent, Args{ "hush" }) };

    EXPECT_EQ(trace.word, "hush");
    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 0);
  }

  TEST(Results, AnIntBecomesTheRunsCode)
  {
    Numeric numeric;

    auto const result{ oxbox::cli::Apply(numeric, Args{ "4" }) };

    EXPECT_EQ(result.Status(), CliStatus::RAN);
    EXPECT_EQ(result.Code(), 4);
  }
}
