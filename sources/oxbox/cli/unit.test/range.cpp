// The erased view: that it forgets the source, keeps the value type,
// converts on the way out, and ends where the source ends.

#include "oxbox/cli/range.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <list>
#include <ranges>
#include <string_view>
#include <string>
#include <vector>

namespace oxbox::cli::unit_test
{
  namespace stdr = std::ranges;

  using namespace std::string_view_literals;

  // The conformance the rest of the library is allowed to rely on. An
  // input_iterator and no more: operator* yields a prvalue, so there is
  // nothing for a forward iterator to hand out a second time.
  static_assert(std::input_iterator<AnyIterator<int>>);
  static_assert(std::sentinel_for<AnyIterator<int>, AnyIterator<int>>);
  static_assert(stdr::input_range<AnyView<int>>);
  static_assert(stdr::input_range<AnyView<std::string>>);
  static_assert(std::semiregular<AnyIterator<int>>);
  static_assert(!std::forward_iterator<AnyIterator<int>>);
  static_assert(std::same_as<std::iter_value_t<AnyIterator<int>>, int>);
  static_assert(std::same_as<std::iter_difference_t<AnyIterator<int>>,
                             std::ptrdiff_t>);
  static_assert(std::same_as<
    decltype(*std::declval<AnyIterator<std::string> const&>()), std::string>);

  TEST(AnyView, WalksAContiguousSource)
  {
    auto const source{ std::vector<int>{ 1, 2, 3 } };
    auto const view{ EraseRange<int>(source) };

    EXPECT_TRUE(stdr::equal(view, source));
  }

  TEST(AnyView, WalksANodeBasedSourceTheSameWay)
  {
    auto const source{ std::list<int>{ 4, 5, 6, 7 } };
    auto const view{ EraseRange<int>(source) };

    EXPECT_EQ(stdr::to<std::vector<int>>(view), (std::vector<int>{ 4, 5, 6, 7 }));
  }

  TEST(AnyView, ConvertsEachElementAsItGoes)
  {
    auto const source{ std::vector<std::string_view>{ "alpha"sv, "beta"sv } };
    auto const view{ EraseRange<std::string>(source) };

    auto out{ std::vector<std::string>{ } };
    for (auto const& item : view)
      out.push_back(item);

    EXPECT_EQ(out, (std::vector<std::string>{ "alpha", "beta" }));
  }

  TEST(AnyView, WorksInARangeFor)
  {
    auto const source{ std::vector<int>{ 10, 20, 30 } };
    auto sum{ 0 };
    for (auto const value : EraseRange<int>(source))
      sum += value;

    EXPECT_EQ(sum, 60);
  }

  TEST(AnyView, AnEmptySourceIsEmptyAndTerminates)
  {
    auto const source{ std::vector<int>{ } };
    auto const view{ EraseRange<int>(source) };

    EXPECT_TRUE(view.empty());
    EXPECT_EQ(stdr::distance(view), 0);
  }

  TEST(AnyView, ADefaultConstructedViewIsEmpty)
  {
    EXPECT_TRUE(AnyView<int>{ }.empty());
  }

  TEST(AnyView, IsBuiltFromAnIteratorPairToo)
  {
    auto const source{ std::vector<int>{ 1, 2, 3, 4 } };
    auto const view{ EraseRange<int>(source.begin() + 1, source.end()) };

    EXPECT_EQ(stdr::to<std::vector<int>>(view), (std::vector<int>{ 2, 3, 4 }));
  }

  TEST(AnyIterator, ADefaultConstructedOneIsTheUniversalEnd)
  {
    auto const source{ std::vector<int>{ 1 } };
    auto walker{ AnyIterator<int>{ source.begin(), source.end() } };

    EXPECT_EQ(AnyIterator<int>{ }, AnyIterator<int>{ });
    EXPECT_NE(walker, AnyIterator<int>{ });
    ++walker;
    EXPECT_EQ(walker, AnyIterator<int>{ });
  }

  TEST(AnyIterator, TwoWalkersOverTheSameSourceAgreeOnPosition)
  {
    auto const source{ std::vector<int>{ 1, 2 } };
    auto const first{ AnyIterator<int>{ source.begin(), source.end() } };
    auto const same{ AnyIterator<int>{ source.begin(), source.end() } };

    EXPECT_EQ(first, same);
  }

  TEST(AnyIterator, ACopyAdvancesIndependently)
  {
    auto const source{ std::vector<int>{ 1, 2, 3 } };
    auto walker{ AnyIterator<int>{ source.begin(), source.end() } };
    auto const trailing{ walker };

    ++walker;
    EXPECT_EQ(*walker, 2);
    EXPECT_EQ(*trailing, 1);
    EXPECT_NE(walker, trailing);
  }

  TEST(AnyIterator, AssignmentClonesRatherThanShares)
  {
    auto const source{ std::vector<int>{ 1, 2, 3 } };
    auto walker{ AnyIterator<int>{ source.begin(), source.end() } };
    auto trailing{ AnyIterator<int>{ } };

    trailing = walker;
    ++walker;
    EXPECT_EQ(*walker, 2);
    EXPECT_EQ(*trailing, 1);
  }

  TEST(AnyIterator, IteratorsOverDifferentSourceKindsAreNeverEqual)
  {
    auto const vector_source{ std::vector<int>{ 1 } };
    auto const list_source{ std::list<int>{ 1 } };

    EXPECT_NE(AnyIterator<int>(vector_source.begin(), vector_source.end()),
              AnyIterator<int>(list_source.begin(), list_source.end()));
  }
}
