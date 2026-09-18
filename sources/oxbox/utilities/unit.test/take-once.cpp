// One rule: the value is handed over once. Pinned here because the drop event
// it was written for cannot be exercised headless.
#include "oxbox/utilities/take-once.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace
{
  using oxbox::utilities::TakeOnce;
}

TEST(TakeOnce, StartsWithNothingToTake)
{
  TakeOnce<std::string> dropped;
  EXPECT_TRUE(dropped.Take().empty());
}

TEST(TakeOnce, HandsTheValueOverExactlyOnce)
{
  TakeOnce<std::string> dropped;
  dropped.Record("/tmp/dropped.bin");

  EXPECT_EQ(dropped.Take(), "/tmp/dropped.bin");
  // taken means gone: a second ask is asking for an event that did not happen
  EXPECT_TRUE(dropped.Take().empty());
}

TEST(TakeOnce, TheLastRecordWins)
{
  TakeOnce<std::string> dropped;
  dropped.Record("/tmp/first.bin");
  dropped.Record("/tmp/second.bin");
  EXPECT_EQ(dropped.Take(), "/tmp/second.bin");
}

TEST(TakeOnce, CarriesAMoveOnlyValueToo)
{
  // the handover is a move, so a move-only type is what this is for
  TakeOnce<std::unique_ptr<int>> held;
  held.Record(std::make_unique<int>(42));
  auto const taken{ held.Take() };
  ASSERT_NE(taken, nullptr);
  EXPECT_EQ(*taken, 42);
  EXPECT_EQ(held.Take(), nullptr);
}
