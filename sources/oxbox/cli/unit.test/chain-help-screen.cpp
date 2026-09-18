// Chain-aware help: the screen is about the line rather than about one
// class, so every layer the line passed through is on it.

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/chain-cli.inc"
#include "oxbox/cli/unit.test/rest-cli.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <string>
#include <vector>

namespace help_test
{
  enum class Tint { OCEAN, CORAL };
  constexpr auto reflect_scheme(Tint*)
  {
    return ::reflect::enum_scheme<
      ::reflect::enumerator<"OCEAN", Tint::OCEAN>,
      ::reflect::enumerator<"CORAL", Tint::CORAL>>{ };
  }

  struct Options : oxbox::cli::Command
  {
    std::string url{ };
    int port{ 80 };
    bool verbose{ false };
    Tint tint{ Tint::OCEAN };
    std::optional<int> limit{ };
  };

  constexpr auto reflect_scheme(Options*)
  {
    using T = Options;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"url", &T::url, "destination", false>,
      ::reflect::member_scheme<"port", &T::port, "port", false>,
      ::reflect::member_scheme<"verbose", &T::verbose, "details", false>,
      ::reflect::member_scheme<"tint", &T::tint, "shade", false>,
      ::reflect::member_scheme<"limit", &T::limit, "cap", false>>{ };
  }
}

namespace
{
  using oxbox::cli::CliStatus;
  using oxbox::cli::chain_test::Chain;
  using oxbox::cli::chain_test::Fresh;
  using oxbox::cli::chain_test::Leaf;
  using oxbox::cli::chain_test::Mid;
  using oxbox::cli::chain_test::Root;
  using Args    = std::vector<std::string_view>;

  TEST(Help, SourceLinesReflowAndBlankLinesSeparateParagraphs)
  {
    using oxbox::cli::detail::rest_cli::Paragraphs;
    EXPECT_EQ(oxbox::cli::FormatHelp<Paragraphs>(),
      "Options:\n"
      "  --folded      This description starts on one source line "
      "and continues on\n"
      "                another source line before ending on a "
      "third source line.\n"
      "                (default: false)\n"
      "  --separate    The first paragraph spans two source lines.\n"
      "                \n"
      "                The second paragraph also spans two source lines.\n"
      "                (default: false)\n"
      "  -h, --help    show this screen\n");
  }

  TEST(Help, WhitespaceOnlyLinesSeparateParagraphs)
  {
    using oxbox::cli::detail::help::Wrap;
    EXPECT_EQ(Wrap("first\n  second\n \t\r\nthird", 12u),
              (std::vector<std::string>{ "first second", "", "third" }));
    EXPECT_TRUE(Wrap("", 12u).empty());
    EXPECT_EQ(Wrap("unbreakable", 3u),
              (std::vector<std::string>{ "unbreakable" }));
  }

  TEST(Help, AMethodWithoutACommentIsListedOnTheParentsBareInvocation)
  {
    oxbox::cli::detail::rest_cli::Undocumented app;
    auto const result{ oxbox::cli::Apply(app, Args{ }) };

    EXPECT_TRUE(result.ShowsScreen());
    EXPECT_NE(result.Message().find("Subcommands:\n  foo\n"),
              std::string_view::npos);
  }

  TEST(Help, ValuePlaceholdersParticipateInColumnAlignment)
  {
    EXPECT_EQ(oxbox::cli::FormatHelp<help_test::Options>(),
      "Options:\n"
      "  --url <string>    destination\n"
      "                    (default: \"\")\n"
      "  --port <int>      port\n"
      "                    (default: 80)\n"
      "  --verbose         details\n"
      "                    (default: false)\n"
      "  --tint <tint>     shade\n"
      "                    one of: ocean, coral; (default: ocean)\n"
      "  --limit <int>     cap\n"
      "                    (default: unset)\n"
      "  -h, --help        show this screen\n");
  }

  // ── chain-aware help: the screen is about the line ───────────────────

  TEST(ChainHelp, ASubcommandsScreenCarriesEveryAncestorsOptionsNearestFirst)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "--leaf", "--help" }) };

    ASSERT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    std::string const screen{ result.Message() };

    auto const own  { screen.find("Options:")          };
    auto const mid  { screen.find("--mid options:")    };
    auto const root_{ screen.find("root options:")     };
    ASSERT_NE(own,   std::string::npos);
    ASSERT_NE(mid,   std::string::npos);
    ASSERT_NE(root_, std::string::npos);

    EXPECT_LT(own, mid);
    EXPECT_LT(mid, root_);

    EXPECT_LT(screen.find("--tone"),     mid);
    EXPECT_LT(mid, screen.find("--level"));
    EXPECT_LT(root_, screen.find("--endpoint"));
  }

  TEST(ChainHelp, TheFrameworksOwnHelpRowIsListedOnceAndNotPerLayer)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(
      root, Args{ "--mid", "--leaf", "--help" }) };

    std::string const screen{ result.Message() };
    EXPECT_NE(screen.find("--help"), std::string::npos);
    EXPECT_EQ(screen.find("--help", screen.find("--help") + 1u),
              std::string::npos);
  }

  TEST(ChainHelp, ALayerNamesItsOwnSubcommandsAndNeverRecursesIntoTheirs)
  {
    Fresh();
    Root root;

    auto const result{ oxbox::cli::Apply(root, Args{ "--mid", "--help" }) };

    std::string const screen{ result.Message() };
    EXPECT_NE(screen.find("Subcommands:"), std::string::npos);
    EXPECT_NE(screen.find("--leaf"), std::string::npos);
    EXPECT_EQ(screen.find("--tone"), std::string::npos);
  }

  TEST(ChainHelp, TheUsageLineGivesEveryLayerItsOwnOptionsSlot)
  {
    // each [options] sits where the line accepts that layer's options
    auto const screen{ oxbox::cli::FormatChainHelp<Leaf, Root, Mid>(
      Args{ "--mid", "--leaf" }, "walker") };

    EXPECT_NE(screen.find("usage: walker [options] --mid [options] "
                          "--leaf [options] [word]"),
              std::string::npos);

    // the program name titles the root's section when the caller knew one
    EXPECT_NE(screen.find("walker options:"), std::string::npos);
    EXPECT_EQ(screen.find("root options:"), std::string::npos);
  }

  TEST(ChainHelp, TheProgramNameTravelsFromArgvToTheScreen)
  {
    // the name is the path's last component and not the path
    Fresh();
    Root root;

    char program[]{ "/usr/local/bin/walker" };
    char mid    []{ "--mid" };
    char help   []{ "--help" };
    char* argv  []{ program, mid, help };

    testing::internal::CaptureStdout();
    auto const result{ oxbox::cli::Main(root, 3, argv) };
    auto const printed{ testing::internal::GetCapturedStdout() };

    EXPECT_EQ(result.Status(), CliStatus::HELP_SHOWN);
    EXPECT_NE(printed.find("usage: walker [options] --mid [options]"),
              std::string::npos);
    EXPECT_NE(printed.find("walker options:"), std::string::npos);
    EXPECT_EQ(printed.find("root options:"), std::string::npos);
    EXPECT_TRUE(Chain().steps.empty());
  }

  TEST(ChainHelp, ARootsOwnScreenIsUnchangedByAnyOfThis)
  {
    // "no chain" is a chain of length one, not a case of its own
    auto const screen{ oxbox::cli::FormatHelp<Root>("walker") };

    EXPECT_NE(screen.find("usage: walker [options]\n"), std::string::npos);
    EXPECT_NE(screen.find("--endpoint"), std::string::npos);
    EXPECT_EQ(screen.find(" options:"), std::string::npos);
  }
}
