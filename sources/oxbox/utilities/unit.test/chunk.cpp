#include "oxbox/utilities/chunk.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <list>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
  namespace fallback = oxbox::utilities::detail::chunk::fallback;

  using oxbox::utilities::Chunk;
  using oxbox::utilities::ChunkSizeZero;

  struct Library  { static constexpr auto const& CHUNK = Chunk; };
  struct Fallback { static constexpr auto const& CHUNK = fallback::Chunk; };

  template <typename _Path>
  class Chunks : public testing::Test { };

  using Paths = testing::Types<Library, Fallback>;
  TYPED_TEST_SUITE(Chunks, Paths);

  template <typename _Range>
  auto Sizes(_Range&& chunks) -> std::vector<std::size_t>
  {
    return std::forward<_Range>(chunks) | std::views::transform(std::ranges::size) | std::ranges::to<std::vector>();
  }

  template <typename _Range>
  auto Flat(_Range&& chunks) -> std::vector<int>
  {
    return std::forward<_Range>(chunks) | std::views::join | std::ranges::to<std::vector>();
  }

  constexpr std::array<int, 7> SEVEN{ 1, 2, 3, 4, 5, 6, 7 };

  template <auto const& CHUNK>
  constexpr auto LastChunkFront() -> int
  {
    auto chunks = CHUNK(SEVEN, 3);
    return *std::ranges::begin(chunks[2]);
  }

  static_assert(LastChunkFront<Chunk>() == 7);
  static_assert(LastChunkFront<fallback::Chunk>() == 7);
  static_assert(std::ranges::size(Chunk(SEVEN, 3)) == 3);
  static_assert(std::ranges::size(fallback::Chunk(SEVEN, 3)) == 3);

  template <typename _Range>
  using ChunkOf = std::ranges::range_reference_t<decltype(Chunk(std::declval<_Range>(), 1))>;

  template <typename _Range>
  using FallbackChunkOf = std::ranges::range_reference_t<decltype(fallback::Chunk(std::declval<_Range>(), 1))>;

  static_assert(std::same_as<ChunkOf<std::span<int>>, FallbackChunkOf<std::span<int>>>);
  static_assert(std::same_as<ChunkOf<std::vector<int>&>, FallbackChunkOf<std::vector<int>&>>);
  static_assert(std::same_as<ChunkOf<std::string_view>, FallbackChunkOf<std::string_view>>);

  static_assert(!std::invocable<decltype(Chunk), std::list<int>&, std::size_t>);
  static_assert(!std::invocable<decltype(fallback::Chunk), std::list<int>&, std::size_t>);

  TYPED_TEST(Chunks, TheLastChunkHoldsTheRemainder)
  {
    auto const chunks = TypeParam::CHUNK(std::span{ SEVEN }, 3);
    EXPECT_EQ(Sizes(chunks), (std::vector<std::size_t>{ 3, 3, 1 }));
    EXPECT_EQ(Flat(chunks), (std::vector<int>{ 1, 2, 3, 4, 5, 6, 7 }));
  }

  TYPED_TEST(Chunks, AnExactMultipleHasNoEmptyTail)
  {
    std::vector<int> const six{ 1, 2, 3, 4, 5, 6 };
    EXPECT_EQ(Sizes(TypeParam::CHUNK(six, 2)), (std::vector<std::size_t>{ 2, 2, 2 }));
  }

  TYPED_TEST(Chunks, AnEmptyRangeHasNoChunks)
  {
    std::vector<int> const none;
    EXPECT_TRUE(std::ranges::empty(TypeParam::CHUNK(none, 4)));
  }

  TYPED_TEST(Chunks, ASizePastTheEndIsOneChunk)
  {
    EXPECT_EQ(Sizes(TypeParam::CHUNK(SEVEN, 100)), (std::vector<std::size_t>{ 7 }));
  }

  TYPED_TEST(Chunks, ChunksWriteThroughToTheSource)
  {
    std::vector<int> stereo{ 1, 2, 3, 4 };
    for (std::span<int> const frame : TypeParam::CHUNK(stereo, 2))
      std::ranges::swap(frame[0], frame[1]);
    EXPECT_EQ(stereo, (std::vector<int>{ 2, 1, 4, 3 }));
  }

  TYPED_TEST(Chunks, ThePipeIsTheCall)
  {
    std::string_view const text{ "abcdefg" };
    auto const as_text = [](auto const& chunk) -> std::string_view { return { chunk.begin(), chunk.end() }; };
    auto const piped   = text | TypeParam::CHUNK(2) | std::views::transform(as_text) | std::ranges::to<std::vector>();
    EXPECT_EQ(piped, (std::vector<std::string_view>{ "ab", "cd", "ef", "g" }));
  }

  TYPED_TEST(Chunks, AnOwnedRangeLivesInTheView)
  {
    EXPECT_EQ(Flat(TypeParam::CHUNK(std::vector<int>{ 1, 2, 3, 4, 5 }, 2)), (std::vector<int>{ 1, 2, 3, 4, 5 }));
  }

  TYPED_TEST(Chunks, ASizeOfZeroIsRefused)
  {
    EXPECT_THROW(static_cast<void>(TypeParam::CHUNK(SEVEN, 0)), ChunkSizeZero);
    EXPECT_THROW(static_cast<void>(SEVEN | TypeParam::CHUNK(0)), ChunkSizeZero);
  }
}
