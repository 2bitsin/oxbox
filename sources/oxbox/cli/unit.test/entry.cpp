// The entry points: which overload of Main greets which argument shape,
// and what escapes Main when the line is refused.
//
// Both halves are here because both were bugs of the same kind -- a
// program that compiled, ran, and did the wrong thing quietly. An argv
// parsed one element too early exits zero having explored the wrong
// listing; a value that will not convert exits 1 with nothing on stderr,
// which reads as "the command ran and failed". Neither shows up in a
// signature, so both are pinned here.

#include "oxbox/cli/main.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>

namespace oxbox::cli::unit_test
{
  // Reports what it was handed. A static rather than a trace pointer,
  // because the argv cases below build the command inside a lambda and
  // what is wanted afterwards is only what it saw.
  struct Recorder : Command
  {
    friend constexpr auto reflect_scheme(Recorder*);
    friend constexpr auto reflect_call_scheme(Recorder*);

    int retries{ 0 };                    /* how many times to try */

    auto operator() (RangeView<std::string> given /* whatever was said */)
      const -> CliResult
    {
      seen.clear();
      for (auto&& one : given) seen.push_back(one);
      return{ };
    }

    static inline std::vector<std::string> seen{ };
  };

  constexpr auto reflect_scheme(Recorder*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"retries", &Recorder::retries,
                               "how many times to try", false>>{ };
  }

  constexpr auto reflect_call_scheme(Recorder*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"given", "whatever was said">>{ };
  }

  // Fails by throwing: the line is fine, the world is not.
  struct Thrower : Command
  {
    friend constexpr auto reflect_scheme(Thrower*);
    friend constexpr auto reflect_call_scheme(Thrower*);

    auto operator() () const -> CliResult
    {
      throw std::runtime_error{ "the api fell over" };
    }
  };

  constexpr auto reflect_scheme(Thrower*)
  {
    return ::reflect::class_scheme<>{ };
  }

  constexpr auto reflect_call_scheme(Thrower*)
  {
    return ::reflect::call_scheme<>{ };
  }

}

namespace
{
  using oxbox::cli::ArgumentVector;
  using oxbox::cli::CliStatus;
  using oxbox::cli::unit_test::Recorder;
  using Strings = std::vector<std::string>;

  // ── what an argv IS, as a compile-time question ──────────────────────
  //
  // The concept is the fix, so the concept is what gets pinned: every
  // const-spelling of every character type main() can arrive in is an
  // argv, and nothing that is a range of strings is.

  static_assert(ArgumentVector<char**>);
  static_assert(ArgumentVector<char* const*>);
  static_assert(ArgumentVector<char const**>);
  static_assert(ArgumentVector<char const* const*>);
  static_assert(ArgumentVector<wchar_t**>);
  static_assert(ArgumentVector<wchar_t const* const*>);
  static_assert(ArgumentVector<char8_t**>);
  static_assert(ArgumentVector<char16_t* const*>);
  static_assert(ArgumentVector<char32_t const**>);

  // A range of strings takes the range path and must keep taking it: this
  // is the assertion that says the argv fix did not eat the ordinary way
  // of calling Main.
  static_assert(!ArgumentVector<std::vector<std::string_view>>);
  static_assert(!ArgumentVector<std::vector<std::string_view>&>);
  static_assert(!ArgumentVector<std::vector<std::string_view> const&>);
  static_assert(!ArgumentVector<std::array<std::string_view, 2>>);
  static_assert(!ArgumentVector<std::string_view const*>);  // one level
  static_assert(!ArgumentVector<char*>);                    // one level
  static_assert(!ArgumentVector<void**>);                   // not strings
  static_assert(!ArgumentVector<int**>);

  // ── and as a run-time one ────────────────────────────────────────────

  TEST(EntryPoints, EveryConstSpellingOfArgvSkipsTheProgramName)
  {
    char program[]{ "recorder" };
    char first  []{ "alpha" };
    char second []{ "beta" };
    char*       mutable_argv[]{ program, first, second };
    char const* readonly_argv[]{ program, first, second };
    constexpr int ARGC{ 3 };

    auto parsed = [](auto argv) {
      Recorder recorder;
      oxbox::cli::Main(recorder, ARGC, argv);
      return Recorder::seen;
    };

    Strings const expected{ "alpha", "beta" };
    EXPECT_EQ(parsed(static_cast<char**            >(mutable_argv )), expected);
    EXPECT_EQ(parsed(static_cast<char* const*      >(mutable_argv )), expected);
    EXPECT_EQ(parsed(static_cast<char const**      >(readonly_argv)), expected);
    EXPECT_EQ(parsed(static_cast<char const* const*>(readonly_argv)), expected);
  }

  TEST(EntryPoints, AnArgvOfNothingButTheProgramNameIsAnEmptyLine)
  {
    char  program[]{ "recorder" };
    char* argv[]{ program };

    Recorder recorder;
    oxbox::cli::Main(recorder, 1, argv);

    EXPECT_TRUE(Recorder::seen.empty());
  }

  TEST(EntryPoints, AnEmptyArgvIsNotIndexedPast)
  {
    char** argv{ nullptr };

    Recorder recorder;
    oxbox::cli::Main(recorder, 0, argv);

    EXPECT_TRUE(Recorder::seen.empty());
  }

  TEST(EntryPoints, ARangeOfStringsHasNoProgramNameToSkip)
  {
    Recorder recorder;
    std::vector<std::string_view> const line{ "alpha", "beta" };

    oxbox::cli::Main(recorder, line);

    EXPECT_EQ(Recorder::seen, Strings({ "alpha", "beta" }));
  }

  TEST(EntryPoints, ACountAndACursorTakeEveryElementFromTheCursorOn)
  {
    Recorder recorder;
    std::array<std::string_view, 2> const line{ "alpha", "beta" };

    oxbox::cli::Main(recorder, line.size(), line.begin());

    EXPECT_EQ(Recorder::seen, Strings({ "alpha", "beta" }));
  }

  TEST(EntryPoints, AnArgvStillFillsTheOptionsBeforeTheRest)
  {
    char program[]{ "recorder" };
    char option []{ "--retries=3" };
    char word   []{ "alpha" };
    char* argv[]{ program, option, word };

    Recorder recorder;
    oxbox::cli::Main(recorder, 3, argv);

    EXPECT_EQ(recorder.retries, 3);
    EXPECT_EQ(Recorder::seen, Strings({ "alpha" }));
  }

  // ── what Main refuses, and how ───────────────────────────────────────

  TEST(EntryPoints, AValueThatWillNotConvertIsAUsageErrorAndNotAnEscape)
  {
    // TypeMismatch is not in the ParseError family and never will be, so
    // Main catches it by name. Before it did, this line escaped Main
    // uncaught: exit 1, and nothing said on either stream.
    Recorder recorder;
    std::vector<std::string_view> const line{ "--retries=abc" };

    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main(recorder, line) };
    std::fflush(stderr);
    auto const complained{ testing::internal::GetCapturedStderr() };

    EXPECT_EQ(result.Status(), CliStatus::USAGE_ERROR);
    EXPECT_EQ(result.Code(), oxbox::cli::USAGE_EXIT_CODE);
    EXPECT_EQ(result.Code(), 2);
    EXPECT_TRUE(complained.starts_with("error: "));
    EXPECT_NE(complained.find("retries"), std::string::npos);
    EXPECT_NE(complained.find("an integer"), std::string::npos);
  }

  TEST(EntryPoints, AParseErrorThroughMainIsRefusedTheVerySameWay)
  {
    // The point of the pair: both families reach one exit code and one
    // stream, so a caller cannot tell from the outside which was raised.
    Recorder recorder;
    std::vector<std::string_view> const line{ "--retires=3" };

    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main(recorder, line) };
    std::fflush(stderr);
    auto const complained{ testing::internal::GetCapturedStderr() };

    EXPECT_EQ(result.Status(), CliStatus::USAGE_ERROR);
    EXPECT_EQ(result.Code(), 2);
    EXPECT_TRUE(complained.starts_with("error: "));
    EXPECT_NE(complained.find("retries"), std::string::npos);   // suggested
  }

  TEST(EntryPoints, ARefusedLineSaysNothingOnStandardOutput)
  {
    Recorder recorder;
    std::vector<std::string_view> const line{ "--retries=abc" };

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    static_cast<void>(oxbox::cli::Main(recorder, line));
    std::fflush(stdout);
    std::fflush(stderr);
    auto const printed{ testing::internal::GetCapturedStdout() };
    static_cast<void>(testing::internal::GetCapturedStderr());

    EXPECT_TRUE(printed.empty());
  }

  TEST(EntryPoints, ABadValueThroughAnArgvIsRefusedToo)
  {
    // the same refusal, arrived at through the shape a real main() has
    char program[]{ "recorder" };
    char option []{ "--retries=abc" };
    char* argv[]{ program, option };

    Recorder recorder;
    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main(recorder, 2, argv) };
    std::fflush(stderr);
    static_cast<void>(testing::internal::GetCapturedStderr());

    EXPECT_EQ(result.Status(), CliStatus::USAGE_ERROR);
    EXPECT_EQ(result.Code(), 2);
  }

  // ── a command's own throw passes through Main ────────────────────────

  TEST(EntryPoints, ACommandsThrowPassesThroughToTheCallersMain)
  {
    // what a failed run means is the application's call, made in its own
    // main(); Main only maps command-line misuse
    oxbox::cli::unit_test::Thrower thrower;
    std::vector<std::string_view> const line{ };

    EXPECT_THROW(static_cast<void>(oxbox::cli::Main(thrower, line)),
                 std::runtime_error);
  }

  TEST(EntryPoints, MisuseIsStillMappedNotThrown)
  {
    // ParseError derives from std::exception, and the typo still comes
    // back as a usage result rather than escaping
    oxbox::cli::unit_test::Thrower thrower;
    std::vector<std::string_view> const line{ "--bogus" };

    testing::internal::CaptureStderr();
    auto const result{ oxbox::cli::Main(thrower, line) };
    std::fflush(stderr);
    static_cast<void>(testing::internal::GetCapturedStderr());

    EXPECT_EQ(result.Status(), CliStatus::USAGE_ERROR);
    EXPECT_EQ(result.Code(), 2);
  }
}
