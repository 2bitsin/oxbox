// The buffering these calls set has no portable way to be read back.
#include "oxbox/cli/console.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace oxbox::cli::unit_test
{
  namespace
  {
    TEST(Console, PreparingTheConsoleIsSafeAndRepeatable)
    {
      PrepareConsole();
      PrepareConsole();
      SetStdoutUnbuffered();

      EXPECT_GT(std::fprintf(stdout, ""), -1);
      EXPECT_GT(std::fprintf(stderr, ""), -1);
    }

    TEST(Console, AttachingSaysWhetherThereWasAConsoleToAttach)
    {
      // on win32 the answer depends on how the process was started
      auto const first{ AttachParentConsole() };
      EXPECT_EQ(AttachParentConsole(), first);
    }
  }
}
