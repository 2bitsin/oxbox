// The template over each kind of base, the id that separates two aliases of
// one shape, and the copy guarantee the base makes.
#include "oxbox/utilities/exception.hpp"

#include "oxbox/utilities/hash.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
  using oxbox::utilities::Exception;
  using namespace oxbox::utilities::literals;

  using RefusedPeer  = Exception<"RefusedPeer"_hash,  std::runtime_error,    "peer refused">;
  using ClosedPeer   = Exception<"ClosedPeer"_hash,   std::runtime_error,    "peer {} refused", std::string>;
  using DroppedPeer  = Exception<"DroppedPeer"_hash,  std::runtime_error,    "peer {} refused", std::string>;
  using BadWidth     = Exception<"BadWidth"_hash,     std::logic_error,      "width {} is not a multiple of {} at {}",
                                 int, int, std::string_view>;
  using BadName      = Exception<"BadName"_hash,      std::invalid_argument, "name '{}'", std::string>;
  using BareFailure  = Exception<"BareFailure"_hash,  std::exception,        "stage {} of {} failed", int, int>;
  using BareNoText   = Exception<"BareNoText"_hash,   std::exception,        "nothing to add">;

  // Default-constructible and string-constructible, but the string is not what().
  class PathAndDefault : public std::exception
  {
  public:
    PathAndDefault() = default;
    explicit PathAndDefault(std::string const& path) : _path{ path } { }

  private:
    std::string _path;
  };

  class FixedRuntimeError : public std::runtime_error
  {
  public:
    FixedRuntimeError() : std::runtime_error{ "fixed" } { }
  };

  class Sealed final : public std::exception
  {
  };

  class CodeOnly : public std::runtime_error
  {
  public:
    explicit CodeOnly(int code) : std::runtime_error{ std::to_string(code) } { }
  };

  class SealedWhatRuntimeError : public std::runtime_error
  {
  public:
    using std::runtime_error::runtime_error;

    [[nodiscard]] auto what() const noexcept -> char const* final { return std::runtime_error::what(); }
  };

  class NoCopy : public std::exception
  {
  public:
    NoCopy() = default;
    NoCopy(NoCopy const&) = delete;
  };

  class NoCopyAssign : public std::exception
  {
  public:
    NoCopyAssign() = default;
    NoCopyAssign(NoCopyAssign const&) = default;

    auto operator=(NoCopyAssign const&) -> NoCopyAssign& = delete;
  };

  class Prefixed : public std::runtime_error
  {
  public:
    explicit Prefixed(std::string const& text) : std::runtime_error{ "fetch failed: " + text } { }
  };

  // Every copy fails, the way a base holding a std::string can when memory runs out.
  class CopyThrows : public std::exception
  {
  public:
    CopyThrows() = default;
    CopyThrows(CopyThrows const& other) : std::exception{ other } { throw std::bad_alloc{ }; }
    CopyThrows(CopyThrows&& other) : CopyThrows{ std::as_const(other) } { }
    ~CopyThrows() override = default;

    auto operator=(CopyThrows const&)  -> CopyThrows& { throw std::bad_alloc{ }; }
    auto operator=(CopyThrows&& other) -> CopyThrows& { return *this = std::as_const(other); }
  };

  using BothFailure       = Exception<"BothFailure"_hash,       PathAndDefault,         "stage {} failed", int>;
  using FixedFailure      = Exception<"FixedFailure"_hash,      FixedRuntimeError,      "stage {} failed", int>;
  using SealedWhatFailure = Exception<"SealedWhatFailure"_hash, SealedWhatRuntimeError, "stage {} failed", int>;
  using CopyFailure       = Exception<"CopyFailure"_hash,       CopyThrows,             "stage {} failed", int>;
  using PrefixedFailure   = Exception<"PrefixedFailure"_hash,   Prefixed,               "stage {} failed", int>;

  template <typename Alias, typename Base>
  constexpr bool COPIES_AS_THE_BASE_DOES
    = std::is_nothrow_copy_constructible_v<Alias> == std::is_nothrow_copy_constructible_v<Base>
   && std::is_nothrow_move_constructible_v<Alias> == std::is_nothrow_copy_constructible_v<Base>
   && std::is_nothrow_copy_assignable_v<Alias>    == std::is_nothrow_copy_assignable_v<Base>
   && std::is_nothrow_move_assignable_v<Alias>    == std::is_nothrow_copy_assignable_v<Base>;

  template <typename Base>
  concept Accepted = requires { typename Exception<"Accepted"_hash, Base, "text">; };
}

static_assert(Accepted<std::exception>);
static_assert(Accepted<PathAndDefault>);
static_assert(Accepted<FixedRuntimeError>);
static_assert(Accepted<SealedWhatRuntimeError>, "a final what() is no obstacle where the base holds the text");
static_assert(!Accepted<Sealed>, "a final base cannot be derived from");
static_assert(!Accepted<CodeOnly>, "neither the text route nor default-constructible");
static_assert(!Accepted<std::string>, "not an exception");
static_assert(!Accepted<NoCopy>, "an exception is copied when thrown");
static_assert(!Accepted<NoCopyAssign>, "the alias copy-assigns the base");

static_assert(std::derived_from<ClosedPeer, std::runtime_error>);
static_assert(std::derived_from<BadWidth, std::logic_error>);
static_assert(std::derived_from<BadName, std::invalid_argument>);
static_assert(std::derived_from<BareFailure, std::exception>);

static_assert(ClosedPeer::Id == "ClosedPeer"_hash);
static_assert(DroppedPeer::Id == "DroppedPeer"_hash);
static_assert(!std::same_as<ClosedPeer, DroppedPeer>,
              "two aliases of one base and format are told apart by the id");
static_assert(!std::derived_from<ClosedPeer, DroppedPeer>);

static_assert(std::is_nothrow_copy_constructible_v<ClosedPeer>
              == std::is_nothrow_copy_constructible_v<std::runtime_error>);
static_assert(std::is_nothrow_copy_constructible_v<BadWidth>
              == std::is_nothrow_copy_constructible_v<std::logic_error>);
static_assert(std::is_nothrow_copy_constructible_v<BadName>
              == std::is_nothrow_copy_constructible_v<std::invalid_argument>);
static_assert(std::is_nothrow_copy_constructible_v<BareFailure>
              == std::is_nothrow_copy_constructible_v<std::exception>);
static_assert(std::is_nothrow_move_constructible_v<BareFailure>);
static_assert(std::is_nothrow_copy_assignable_v<BareFailure>);
static_assert(std::is_nothrow_move_assignable_v<BareFailure>);

static_assert(!std::is_nothrow_copy_constructible_v<PathAndDefault>, "the path's copy can throw");
static_assert(COPIES_AS_THE_BASE_DOES<BothFailure, PathAndDefault>);
static_assert(COPIES_AS_THE_BASE_DOES<CopyFailure, CopyThrows>);
static_assert(COPIES_AS_THE_BASE_DOES<BareFailure, std::exception>);

static_assert(!std::is_convertible_v<std::string, ClosedPeer>, "the constructor is explicit");
static_assert(!std::is_constructible_v<ClosedPeer>, "the arguments are the format's, all of them");

TEST(Exception, ZeroArgumentsTheFormatIsTheText)
{
  EXPECT_STREQ(RefusedPeer{ }.what(), "peer refused");
}

TEST(Exception, OneArgumentOverARuntimeError)
{
  EXPECT_STREQ(ClosedPeer{ "10.0.0.7" }.what(), "peer 10.0.0.7 refused");
}

TEST(Exception, ThreeArgumentsOverALogicError)
{
  EXPECT_STREQ(BadWidth(642, 4, "Resize").what(), "width 642 is not a multiple of 4 at Resize");
}

TEST(Exception, AChildOfALogicErrorHoldsTheTextToo)
{
  EXPECT_STREQ(BadName{ "x y" }.what(), "name 'x y'");
}

TEST(Exception, ABareExceptionBaseStillAnswersTheText)
{
  BareFailure const failure{ 2, 5 };
  std::exception const& base{ failure };
  EXPECT_STREQ(base.what(), "stage 2 of 5 failed");
  EXPECT_STREQ(BareNoText{ }.what(), "nothing to add");
}

TEST(Exception, ACopyOfABareExceptionBaseKeepsTheText)
{
  BareFailure const failure{ 3, 4 };
  BareFailure const copy{ failure };
  EXPECT_STREQ(copy.what(), "stage 3 of 4 failed");
}

TEST(Exception, ACopyOutlivesTheOriginal)
{
  auto original{ std::make_unique<BareFailure>(3, 4) };
  BareFailure const copy{ *original };
  original.reset();
  EXPECT_STREQ(copy.what(), "stage 3 of 4 failed");
}

TEST(Exception, AMovedFromBareExceptionStillAnswersTheText)
{
  BareFailure source{ 2, 5 };
  BareFailure const moved{ std::move(source) };
  EXPECT_STREQ(moved.what(), "stage 2 of 5 failed");
  // NOLINTNEXTLINE(bugprone-use-after-move): the moved-from state is the test
  EXPECT_STREQ(source.what(), "stage 2 of 5 failed");
}

TEST(Exception, AMoveAssignedFromBareExceptionStillAnswersTheText)
{
  BareFailure source{ 2, 5 };
  BareFailure target{ 1, 1 };
  target = std::move(source);
  EXPECT_STREQ(target.what(), "stage 2 of 5 failed");
  // NOLINTNEXTLINE(bugprone-use-after-move): the moved-from state is the test
  EXPECT_STREQ(source.what(), "stage 2 of 5 failed");
}

TEST(Exception, AStringConstructibleBaseOutsideTheTwoFamiliesGetsTheTextHeld)
{
  EXPECT_STREQ(BothFailure{ 5 }.what(), "stage 5 failed");
}

TEST(Exception, ARuntimeErrorChildWithoutAStringConstructorGetsTheTextHeld)
{
  EXPECT_STREQ(FixedFailure{ 6 }.what(), "stage 6 failed");
}

TEST(Exception, ARuntimeErrorChildWithAFinalWhatHoldsTheText)
{
  EXPECT_STREQ(SealedWhatFailure{ 7 }.what(), "stage 7 failed");
}

TEST(Exception, ARuntimeErrorChildWithAStringConstructorMakesWhatOfTheText)
{
  EXPECT_STREQ(PrefixedFailure{ 3 }.what(), "fetch failed: stage 3 failed");
}

TEST(Exception, ACopyOverABaseWhoseCopyThrowsPropagatesTheThrow)
{
  CopyFailure const failure{ 1 };
  CopyFailure       target{ 2 };
  CopyFailure       source{ 3 };
  EXPECT_THROW(CopyFailure{ failure }, std::bad_alloc);
  EXPECT_THROW(CopyFailure{ std::move(source) }, std::bad_alloc);
  EXPECT_THROW(target = failure, std::bad_alloc);
  EXPECT_THROW(target = CopyFailure{ 4 }, std::bad_alloc);
}

TEST(Exception, CaughtByItsBase)
{
  EXPECT_THROW(throw ClosedPeer{ "10.0.0.7" }, std::runtime_error);
  EXPECT_THROW(throw BadWidth(1, 2, "Resize"), std::logic_error);
  EXPECT_THROW(throw BareFailure(1, 2), std::exception);
}

TEST(Exception, AnAliasOfAnotherIdIsNotCaught)
{
  auto const throws_closed = [] -> void {
    try
    {
      throw ClosedPeer{ "10.0.0.7" };
    }
    catch (DroppedPeer const&)
    {
      FAIL() << "a ClosedPeer was caught as a DroppedPeer";
    }
  };
  EXPECT_THROW(throws_closed(), ClosedPeer);
}
