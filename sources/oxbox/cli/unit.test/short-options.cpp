#include "oxbox/cli/help.hpp"
#include "oxbox/cli/parse.hpp"
#include "oxbox/cli/unit.test/short-cli.hpp"

#include <gtest/gtest.h>

namespace
{
  using namespace oxbox::cli;
  using namespace oxbox::cli::short_test;

  TEST(ShortOptions, BoolAndBothValueForms)
  {
    Options options;
    ParseOptions(options, { "-v", "-o", "file" });
    EXPECT_TRUE(options.verbose);
    EXPECT_EQ(options.output, "file");
    Options attached;
    ParseOptions(attached, { "-o=other" });
    EXPECT_EQ(attached.output, "other");
    EXPECT_THROW(ParseOptions(attached, { "-v", "--verbose" }),
                 RepeatedOption);
  }

  TEST(ShortOptions, HelpAndClaimedHelp)
  {
    EXPECT_TRUE(ScanOptions<Options>({ "-h" }).help_requested);
    Claimed claimed;
    EXPECT_FALSE(ParseOptions(claimed, { "-h" }).help_requested);
    EXPECT_TRUE(claimed.hush);
    EXPECT_TRUE(ScanOptions<Claimed>({ "--help" }).help_requested);
    EXPECT_THROW(ScanOptions<OwnHelp>({ "-h" }), UnknownOption);
    OwnHelp own;
    EXPECT_FALSE(ParseOptions(own, { "--help" }).help_requested);
    EXPECT_TRUE(own.help);
  }

  TEST(ShortOptions, NoBundlingAndExactSpelling)
  {
    try {
      ScanOptions<Options>({ "-abc" });
      FAIL() << "expected an unknown option";
    } catch (UnknownOption const& error) {
      EXPECT_EQ(error.option, "-abc");
      EXPECT_STREQ(error.what(), "unknown option '-abc'");
    }
    EXPECT_THROW(ScanOptions<Options>({ "-V" }), UnknownOption);
    EXPECT_THROW(ScanOptions<Options>({ "-verbose" }), UnknownOption);
  }

  TEST(ShortOptions, LoneDashAndSentinelStayPositional)
  {
    auto const result{ ScanOptions<Options>({ "-", "--", "-v" }) };
    EXPECT_EQ(result.positionals,
              (std::vector<std::string_view>{ "-", "-v" }));
  }

  TEST(ShortOptions, HelpRowsIncludeAliasesInTheirWidth)
  {
    EXPECT_EQ(FormatHelp<Options>(),
      "Options:\n"
      "  -v, --verbose            say what is being done\n"
      "                           (default: false)\n"
      "  -o, --output <string>    destination\n"
      "                           (default: \"\")\n"
      "  -h, --help               show this screen\n");
    EXPECT_EQ(FormatHelp<Claimed>(),
      "Options:\n"
      "  -h, --hush    be quiet\n"
      "                (default: false)\n"
      "  --help        show this screen\n");
    EXPECT_EQ(FormatHelp<OwnHelp>(),
      "Options:\n"
      "  --help    application help\n"
      "            (default: false)\n");
  }
}
