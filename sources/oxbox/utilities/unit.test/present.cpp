#include "oxbox/utilities/present.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace
{
  namespace fallback = oxbox::utilities::detail::present::fallback;

  using oxbox::utilities::Present;

  struct Library  { static constexpr auto const& PRESENT = Present; };
  struct Fallback { static constexpr auto const& PRESENT = fallback::Present; };

  template <typename _Path>
  class Presented : public testing::Test { };

  using Paths = testing::Types<Library, Fallback>;
  TYPED_TEST_SUITE(Presented, Paths);

  constexpr std::array<std::optional<int>, 5> MAYBE{ 3, std::nullopt, 4, std::nullopt, 5 };

  static_assert(std::ranges::equal(Present(MAYBE), std::array{ 3, 4, 5 }));
  static_assert(std::ranges::equal(fallback::Present(MAYBE), std::array{ 3, 4, 5 }));
  static_assert(std::ranges::equal(MAYBE | fallback::Present, std::array{ 3, 4, 5 }));

  template <typename _Range>
  using LibraryElement = std::ranges::range_reference_t<decltype(Present(std::declval<_Range>()))>;

  template <typename _Range>
  using FallbackElement = std::ranges::range_reference_t<decltype(fallback::Present(std::declval<_Range>()))>;

  static_assert(std::same_as<FallbackElement<std::vector<std::optional<int>>&>, int&>);
  static_assert(std::same_as<LibraryElement<std::vector<std::optional<int>>&>,
                             FallbackElement<std::vector<std::optional<int>>&>>);
  static_assert(std::same_as<LibraryElement<std::vector<std::optional<int>> const&>,
                             FallbackElement<std::vector<std::optional<int>> const&>>);

  using Rvalues = decltype(std::declval<std::vector<std::optional<std::string>>&>() | std::views::as_rvalue);
  static_assert(std::same_as<FallbackElement<Rvalues>, std::string&>);
  static_assert(std::same_as<LibraryElement<Rvalues>, FallbackElement<Rvalues>>);

  constexpr bool IS_FALLBACK{ std::same_as<oxbox::utilities::detail::present::Presenter, fallback::Presenter> };
  using Lvalues   = std::vector<std::optional<int>>&;
  static_assert(!std::ranges::range<decltype(fallback::Present(std::declval<Lvalues>())) const>);
  static_assert(std::ranges::range<decltype(Present(std::declval<Lvalues>())) const> != IS_FALLBACK);

  static_assert(!std::invocable<decltype(Present), std::vector<std::vector<int>>&>);
  static_assert(!std::invocable<decltype(fallback::Present), std::vector<std::vector<int>>&>);

  TYPED_TEST(Presented, SkipsTheDisengagedInOrder)
  {
    std::vector<std::optional<std::string>> const events{ std::nullopt, "open", std::nullopt, "close" };
    EXPECT_EQ(TypeParam::PRESENT(events) | std::ranges::to<std::vector>(),
              (std::vector<std::string>{ "open", "close" }));
  }

  TYPED_TEST(Presented, AllDisengagedIsEmpty)
  {
    std::array<std::optional<int>, 2> const none{ };
    EXPECT_TRUE(std::ranges::empty(TypeParam::PRESENT(none)));
  }

  TYPED_TEST(Presented, TheValuesAreTheContainedOnes)
  {
    std::array<std::optional<int>, 3> slots{ 1, std::nullopt, 2 };
    for (int& value : slots | TypeParam::PRESENT)
      value *= 10;
    EXPECT_EQ(slots[0], 10);
    EXPECT_EQ(slots[1], std::nullopt);
    EXPECT_EQ(slots[2], 20);
  }

  TYPED_TEST(Presented, CopiesIntoAnOutputLikeTheCallSites)
  {
    std::array<std::optional<int>, 4> const handles{ std::nullopt, 7, 8, std::nullopt };
    std::array<int, 4> out{ };
    auto const next = std::ranges::copy(TypeParam::PRESENT(handles), out.begin()).out;
    EXPECT_EQ(next - out.begin(), 2);
    EXPECT_EQ(out[0], 7);
    EXPECT_EQ(out[1], 8);
  }

  TYPED_TEST(Presented, AnRvalueViewLeavesTheSourceIntact)
  {
    std::vector<std::optional<std::string>> source{ std::string(40, 'a'), std::nullopt };
    std::size_t seen{ 0 };
    for (auto&& text : source | std::views::as_rvalue | TypeParam::PRESENT)
      seen += text.size();
    EXPECT_EQ(seen, 40u);
    ASSERT_TRUE(source[0].has_value());
    EXPECT_EQ(source[0]->size(), 40u);
  }

  TYPED_TEST(Presented, APrvalueOptionalGivesUpItsValue)
  {
    std::vector<int> const numbers{ 1, 2, 3, 4 };
    auto const evens = numbers
                     | std::views::transform([](int number) -> std::optional<std::unique_ptr<int>> {
                         if (number % 2 != 0) { return std::nullopt; }
                         return std::make_unique<int>(number);
                       })
                     | TypeParam::PRESENT
                     | std::views::transform([](auto&& owned) -> int { return *owned; })
                     | std::ranges::to<std::vector>();
    EXPECT_EQ(evens, (std::vector<int>{ 2, 4 }));
  }
}
