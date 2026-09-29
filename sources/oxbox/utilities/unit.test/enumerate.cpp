#include "oxbox/utilities/enumerate.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <forward_list>
#include <list>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
  namespace fallback = oxbox::utilities::detail::enumerate::fallback;

  using oxbox::utilities::Enumerate;

  struct Library  { static constexpr auto const& ENUMERATE = Enumerate; };
  struct Fallback { static constexpr auto const& ENUMERATE = fallback::Enumerate; };

  template <typename _Path>
  class Enumerated : public testing::Test { };

  using Paths = testing::Types<Library, Fallback>;
  TYPED_TEST_SUITE(Enumerated, Paths);

  constexpr std::array<int, 3> VALUES{ 5, 7, 9 };

  constexpr auto IndexWeighted(auto&& enumerated) -> std::ptrdiff_t
  {
    std::ptrdiff_t sum{ 0 };
    for (auto const [index, value] : enumerated)
      sum += index * value;
    return sum;
  }

  static_assert(IndexWeighted(Enumerate(VALUES)) == 0 * 5 + 1 * 7 + 2 * 9);
  static_assert(IndexWeighted(fallback::Enumerate(VALUES)) == 0 * 5 + 1 * 7 + 2 * 9);
  static_assert(IndexWeighted(VALUES | fallback::Enumerate) == 25);

  template <typename _Range>
  using LibraryElement = std::ranges::range_reference_t<decltype(Enumerate(std::declval<_Range>()))>;

  template <typename _Range>
  using FallbackElement = std::ranges::range_reference_t<decltype(fallback::Enumerate(std::declval<_Range>()))>;

  static_assert(std::same_as<FallbackElement<std::vector<int>&>, std::tuple<std::ptrdiff_t, int&>>);
  static_assert(std::same_as<LibraryElement<std::vector<int>&>, FallbackElement<std::vector<int>&>>);
  static_assert(std::same_as<LibraryElement<std::vector<int> const&>, FallbackElement<std::vector<int> const&>>);

  template <typename _Range>
  using FallbackOver = decltype(fallback::Enumerate(std::declval<_Range>()));

  static_assert(std::same_as<std::ranges::range_value_t<decltype(Enumerate(std::declval<std::vector<int>&>()))>,
                             std::ranges::range_value_t<FallbackOver<std::vector<int>&>>>);

  static_assert(std::ranges::sized_range<FallbackOver<std::array<int, 3> const&>>);
  static_assert(std::ranges::common_range<FallbackOver<std::array<int, 3> const&>>);
  static_assert(std::ranges::sized_range<FallbackOver<std::list<int>&>>);
  static_assert(!std::ranges::common_range<FallbackOver<std::list<int>&>>);

  TYPED_TEST(Enumerated, CountsFromZeroInOrder)
  {
    std::vector<std::string> const names{ "a", "b", "c" };
    std::vector<std::pair<std::ptrdiff_t, std::string>> seen;
    for (auto const& [index, name] : TypeParam::ENUMERATE(names))
      seen.emplace_back(index, name);
    EXPECT_EQ(seen, (std::vector<std::pair<std::ptrdiff_t, std::string>>{ { 0, "a" }, { 1, "b" }, { 2, "c" } }));
  }

  TYPED_TEST(Enumerated, TheElementIsTheSourceItself)
  {
    std::vector<int> values{ 10, 20, 30 };
    for (auto [index, value] : values | TypeParam::ENUMERATE)
      value += static_cast<int>(index);
    EXPECT_EQ(values, (std::vector<int>{ 10, 21, 32 }));
  }

  TYPED_TEST(Enumerated, AnUnsizedRangeIsCountedToo)
  {
    std::forward_list<char> const letters{ 'x', 'y' };
    auto const index   = [](auto const& element) -> std::ptrdiff_t { return std::get<0>(element); };
    auto const indices = TypeParam::ENUMERATE(letters) | std::views::transform(index) | std::ranges::to<std::vector>();
    EXPECT_EQ(indices, (std::vector<std::ptrdiff_t>{ 0, 1 }));
  }

  TYPED_TEST(Enumerated, AnEmptyRangeYieldsNothing)
  {
    std::vector<int> const none;
    EXPECT_TRUE(std::ranges::empty(TypeParam::ENUMERATE(none)));
  }
}
