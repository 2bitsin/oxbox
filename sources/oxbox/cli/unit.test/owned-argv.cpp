// What a C API's argv is built from, and the property the type exists for:
// the bytes it hands out are its own.
#include "oxbox/cli/owned-argv.hpp"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <vector>

namespace oxbox::cli::unit_test
{
  namespace
  {
    TEST(OwnedArgv, IsArgvZeroPlusWhatItWasGiven)
    {
      std::vector<std::string> const tail{ "--ozone-platform=headless",
                                           "--no-sandbox" };
      OwnedArgv args{ "/opt/oxbox/program", tail };

      ASSERT_EQ(args.Argc(), 3);
      EXPECT_STREQ(args.Argv()[0], "/opt/oxbox/program");
      EXPECT_STREQ(args.Argv()[1], "--ozone-platform=headless");
      EXPECT_STREQ(args.Argv()[2], "--no-sandbox");
      // the terminator is part of the contract
      EXPECT_EQ(args.Argv()[3], nullptr);
    }

    TEST(OwnedArgv, WithNoArgumentsIsJustTheProgram)
    {
      OwnedArgv args{ "program", { } };
      ASSERT_EQ(args.Argc(), 1);
      EXPECT_STREQ(args.Argv()[0], "program");
      EXPECT_EQ(args.Argv()[1], nullptr);
    }

    // a long argument (past the small-string buffer) and a short one both have
    // to read back after the caller's own strings have gone
    TEST(OwnedArgv, OwnsTheBytesItHandsOut)
    {
      std::vector<std::string> tail{
        "--js-flags=--max-old-space-size=4096 --expose-gc", "-x" };
      OwnedArgv args{ "program", tail };
      tail.clear();

      EXPECT_STREQ(args.Argv()[1],
                   "--js-flags=--max-old-space-size=4096 --expose-gc");
      EXPECT_STREQ(args.Argv()[2], "-x");
    }

    // neither a copy nor a move, and for a short string it would fail silently
    static_assert(!std::is_copy_constructible_v<OwnedArgv>);
    static_assert(!std::is_move_constructible_v<OwnedArgv>);
    static_assert(!std::is_copy_assignable_v<OwnedArgv>);
    static_assert(!std::is_move_assignable_v<OwnedArgv>);
  }
}
