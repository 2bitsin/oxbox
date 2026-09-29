#include "oxbox/utilities/function.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
  namespace fallback = oxbox::utilities::detail::function::fallback;

  struct Library
  {
    template <typename _Signature>
    using Function = oxbox::utilities::MoveOnlyFunction<_Signature>;
  };

  struct Fallback
  {
    template <typename _Signature>
    using Function = fallback::MoveOnlyFunction<_Signature>;
  };

  template <typename _Path>
  class MoveOnlyFunctions : public testing::Test { };

  using Paths = testing::Types<Library, Fallback>;
  TYPED_TEST_SUITE(MoveOnlyFunctions, Paths);

  struct MutableOnly
  {
    auto operator()() -> int { return 1; }
  };

  struct MayThrow
  {
    auto operator()() const -> int { return 1; }
  };

  struct LValueOnly
  {
    auto operator()() & -> int { return 1; }
  };

  struct Counted
  {
    explicit Counted(std::shared_ptr<int> destroyed) : _destroyed{ std::move(destroyed) } { }
    Counted(Counted&&) noexcept                    = default;
    Counted(Counted const&)                        = delete;
    auto operator=(Counted&&) noexcept -> Counted& = default;
    auto operator=(Counted const&) -> Counted&     = delete;
    ~Counted() { if (_destroyed) { ++*_destroyed; } }

    auto operator()() const -> int { return 0; }

    std::shared_ptr<int> _destroyed;
  };

  struct Sum
  {
    Sum(std::initializer_list<int> values, int offset) : _total{ offset }
    { for (int const value : values) { _total += value; } }

    auto operator()() const -> int { return _total; }

    int _total;
  };

  template <template <typename> typename F>
  constexpr auto Shapes() -> bool
  {
    static_assert(!std::is_copy_constructible_v<F<int()>>);
    static_assert(std::is_nothrow_move_constructible_v<F<int()>>);
    static_assert(std::same_as<typename F<long(int)>::result_type, long>);

    static_assert(std::is_constructible_v<F<int()>, MutableOnly>);
    static_assert(!std::is_constructible_v<F<int() const>, MutableOnly>);
    static_assert(std::is_constructible_v<F<int() const>, MayThrow>);

    static_assert(!std::is_constructible_v<F<int() noexcept>, MayThrow>);
    static_assert(std::is_nothrow_invocable_v<F<int() noexcept>&>);
    static_assert(!std::is_nothrow_invocable_v<F<int()>&>);

    static_assert(std::is_invocable_v<F<int() &>&>);
    static_assert(!std::is_invocable_v<F<int() &>>);
    static_assert(std::is_invocable_v<F<int() &&>>);
    static_assert(!std::is_invocable_v<F<int() &&>&>);
    static_assert(std::is_invocable_v<F<int() const&> const&>);
    static_assert(std::is_invocable_v<F<int() const&&> const>);
    static_assert(!std::is_invocable_v<F<int()>&, int>);

    static_assert(std::is_nothrow_invocable_v<F<int() & noexcept>&>);
    static_assert(!std::is_invocable_v<F<int() & noexcept>>);
    static_assert(std::is_nothrow_invocable_v<F<int() const& noexcept> const&>);
    static_assert(std::is_nothrow_invocable_v<F<int() && noexcept>>);
    static_assert(!std::is_invocable_v<F<int() && noexcept>&>);
    static_assert(std::is_nothrow_invocable_v<F<int() const&& noexcept> const>);
    static_assert(!std::is_constructible_v<F<int() const& noexcept>, MayThrow>);

    static_assert(std::is_constructible_v<F<int() &>, LValueOnly>);
    static_assert(!std::is_constructible_v<F<int() &&>, LValueOnly>);
    return true;
  }

  static_assert(Shapes<Library::Function>());
  static_assert(Shapes<Fallback::Function>());

  TYPED_TEST(MoveOnlyFunctions, DefaultAndNullAreEmpty)
  {
    using F = typename TypeParam::template Function<int()>;
    F const none;
    F const null{ nullptr };
    EXPECT_FALSE(none);
    EXPECT_TRUE(null == nullptr);
  }

  TYPED_TEST(MoveOnlyFunctions, ANullPointerIsEmptyToo)
  {
    int (*const pointer)() = nullptr;
    int (std::string::*const member)() const = nullptr;
    EXPECT_FALSE((typename TypeParam::template Function<int()>{ pointer }));
    EXPECT_FALSE((typename TypeParam::template Function<int(std::string const&)>{ member }));
  }

  TYPED_TEST(MoveOnlyFunctions, AnEmptyWrapperOfAnotherSignatureIsEmptyToo)
  {
    typename TypeParam::template Function<long()> const wrapped{ typename TypeParam::template Function<int()>{ } };
    EXPECT_FALSE(wrapped);
  }

  TYPED_TEST(MoveOnlyFunctions, NoexceptMeetsEveryReferenceQualifier)
  {
    struct Which
    {
      auto operator()() & noexcept       -> int { return 1; }
      auto operator()() const& noexcept  -> int { return 2; }
      auto operator()() && noexcept      -> int { return 3; }
      auto operator()() const&& noexcept -> int { return 4; }
    };
    typename TypeParam::template Function<int() & noexcept>        lvalue{ Which{ } };
    typename TypeParam::template Function<int() const& noexcept>   const_lvalue{ Which{ } };
    typename TypeParam::template Function<int() && noexcept>       rvalue{ Which{ } };
    typename TypeParam::template Function<int() const&& noexcept>  const_rvalue{ Which{ } };
    EXPECT_EQ(lvalue(), 1);
    EXPECT_EQ(std::as_const(const_lvalue)(), 2);
    EXPECT_EQ(std::move(rvalue)(), 3);
    EXPECT_EQ(std::move(std::as_const(const_rvalue))(), 4);
  }

  TYPED_TEST(MoveOnlyFunctions, SwapsWhenTheSignatureNamesAStandardType)
  {
    using F = typename TypeParam::template Function<std::string(std::string const&)>;
    F upper{ [](std::string const& text) -> std::string { return text + "!"; } };
    F none;
    swap(upper, none);
    EXPECT_FALSE(upper);
    EXPECT_EQ(none("hi"), "hi!");
  }

  TYPED_TEST(MoveOnlyFunctions, HoldsAMoveOnlyCallable)
  {
    typename TypeParam::template Function<int(int)> add{ [base = std::make_unique<int>(40)](int more) -> int {
      return *base + more;
    } };
    ASSERT_TRUE(add);
    EXPECT_EQ(add(2), 42);

    auto moved{ std::move(add) };
    EXPECT_EQ(moved(3), 43);
  }

  TYPED_TEST(MoveOnlyFunctions, ForwardsMoveOnlyArgumentsAndConvertsTheResult)
  {
    typename TypeParam::template Function<long(std::unique_ptr<int>)> take{ [](std::unique_ptr<int> owned) -> int {
      return *owned;
    } };
    EXPECT_EQ(take(std::make_unique<int>(7)), 7L);

    std::vector<int> seen;
    typename TypeParam::template Function<void(int)> discard{ [&seen](int value) -> int {
      seen.push_back(value);
      return value;
    } };
    discard(5);
    EXPECT_EQ(seen, std::vector<int>{ 5 });
  }

  TYPED_TEST(MoveOnlyFunctions, TheQualifiersReachTheTarget)
  {
    struct Which
    {
      auto operator()() &       -> std::string { return "&"; }
      auto operator()() const&  -> std::string { return "const&"; }
      auto operator()() &&      -> std::string { return "&&"; }
      auto operator()() const&& -> std::string { return "const&&"; }
    };
    typename TypeParam::template Function<std::string()>         plain{ Which{ } };
    typename TypeParam::template Function<std::string() const>   constant{ Which{ } };
    typename TypeParam::template Function<std::string() &&>      rvalue{ Which{ } };
    typename TypeParam::template Function<std::string() const&&> const_rvalue{ Which{ } };
    EXPECT_EQ(plain(), "&");
    EXPECT_EQ(std::as_const(constant)(), "const&");
    EXPECT_EQ(std::move(rvalue)(), "&&");
    EXPECT_EQ(std::move(std::as_const(const_rvalue))(), "const&&");
  }

  TYPED_TEST(MoveOnlyFunctions, AssignsSwapsAndEmpties)
  {
    using F = typename TypeParam::template Function<int() const>;
    F one{ [] -> int { return 1; } };
    F two;
    two = [] -> int { return 2; };
    swap(one, two);
    EXPECT_EQ(one(), 2);
    EXPECT_EQ(two(), 1);
    one.swap(two);
    EXPECT_EQ(one(), 1);
    one = nullptr;
    EXPECT_FALSE(one);
  }

  TYPED_TEST(MoveOnlyFunctions, DestroysItsTargetOnce)
  {
    auto const destroyed{ std::make_shared<int>(0) };
    {
      typename TypeParam::template Function<int() const> held{ std::in_place_type<Counted>, destroyed };
      EXPECT_EQ(*destroyed, 0);
      held = nullptr;
      EXPECT_EQ(*destroyed, 1);
    }
    EXPECT_EQ(*destroyed, 1);
  }

  TYPED_TEST(MoveOnlyFunctions, BuildsInPlaceFromAList)
  {
    typename TypeParam::template Function<int() const> sum{ std::in_place_type<Sum>, { 1, 2, 3 }, 10 };
    EXPECT_EQ(sum(), 16);
  }
}
