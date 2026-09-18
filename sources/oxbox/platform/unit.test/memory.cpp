#include "oxbox/platform/memory.hpp"

#include "fixtures.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace oxbox;

using platform::PageAlignedArray;
using platform::test::Contains;
using platform::test::ReasonOf;
using platform::test::UNMAPPABLE;
using utilities::U32;

TEST(PlatformMemory, PageSizeIsSane)
{
  EXPECT_GE(platform::PageSize(), platform::PAGE_ALIGNMENT);
}

TEST(PlatformMemory, AllocationsArePageAligned)
{
  PageAlignedArray<U32> const numbers{ 3u };
  EXPECT_EQ(std::bit_cast<std::uintptr_t>(numbers.data()) % platform::PAGE_ALIGNMENT, 0u);
  EXPECT_EQ(numbers.size(), 3u);
}

TEST(PlatformMemory, FreshPagesReadAsZero)
{
  PageAlignedArray<U32> const numbers{ 1024u };
  for (auto const value : numbers) EXPECT_EQ(value, 0u);
}

TEST(PlatformMemory, FillConstructionFills)
{
  PageAlignedArray<U32> const numbers{ 8u, 0xDEADBEEFu };
  for (auto const value : numbers) EXPECT_EQ(value, 0xDEADBEEFu);
}

TEST(PlatformMemory, CopiesAreIndependentAllocations)
{
  PageAlignedArray<U32> original{ 4u, 7u };
  PageAlignedArray<U32> copy{ original };
  copy[0u] = 42u;
  EXPECT_EQ(original[0u], 7u);
  EXPECT_EQ(copy[0u], 42u);
  EXPECT_NE(original.data(), copy.data());
}

TEST(PlatformMemory, MovesStealTheAllocation)
{
  PageAlignedArray<U32> source{ 4u, 9u };
  auto const* home{ source.data() };
  PageAlignedArray<U32> landed{ std::move(source) };
  EXPECT_EQ(landed.data(), home);
  EXPECT_TRUE(source.empty());
}

TEST(PlatformMemory, EmptyIsALegalState)
{
  PageAlignedArray<U32> const nothing{ 0u };
  EXPECT_TRUE(nothing.empty());
  EXPECT_EQ(nothing.data(), nullptr);
}

// The one failure path: a reservation the kernel will not make. It has to
// arrive as a runtime_error naming the size, not as a null pointer the
// caller then writes through -- this is what a large buffer gets built on,
// so a silent failure is a segfault somewhere far away from the cause.
TEST(PlatformMemory, AReservationTooLargeToMapFailsByName)
{
  try {
    PageAlignedArray<U32> const refused{ UNMAPPABLE };
    FAIL() << "expected a throw";
  } catch (std::runtime_error const& error) {
    // the size, not the syscall: mmap and VirtualAlloc are named by their
    // own backends, and what a reader of the message needs is what was asked
    // for -- followed by what the OS said about it
    EXPECT_TRUE(Contains(error.what(),
                         std::format("{:#x}", UNMAPPABLE * sizeof(U32))))
      << error.what();
    EXPECT_FALSE(ReasonOf(error.what()).empty()) << error.what();
  }
}
