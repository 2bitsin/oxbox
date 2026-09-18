// The one write-road failure that needs /dev/full, which macOS does not have.

#include "oxbox/platform/file-writer.hpp"

#include "fixtures.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <stdexcept>

using namespace oxbox;

using platform::WriteBinaryFile;
using platform::test::Contains;
using platform::test::ReasonOf;
using utilities::Bytes;

TEST(WriteBinaryFile, WriteBinaryFileReportsAWriteTheKernelRefused)
{
  // /dev/full opens and truncates happily and fails every write with ENOSPC.
  // A write() returning zero for a non-empty buffer must break out, not retry.
  std::array<std::byte, 64u> payload{ };
  try {
    WriteBinaryFile("/dev/full", Bytes{ payload });
    FAIL() << "expected a throw";
  } catch (std::runtime_error const& error) {
    EXPECT_TRUE(Contains(error.what(), "write to"))  << error.what();
    EXPECT_TRUE(Contains(error.what(), "/dev/full")) << error.what();
    EXPECT_FALSE(ReasonOf(error.what()).empty())     << error.what();
  }
}
