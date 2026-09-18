// The door is only one door if nothing else includes boost: asio's types
// change shape with _WIN32_WINNT, so a second spelling is an ODR violation
// that links. This suite reads the module's own sources to check that.
#include "oxbox/http/asio.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
  namespace fs = std::filesystem;

  constexpr std::string_view DOOR{ "asio.hpp" };

  // buildutil runs every test process with the module directory as its cwd
  auto ModuleSources() -> std::vector<fs::path>
  {
    std::vector<fs::path> found;
    for (auto const& entry : fs::recursive_directory_iterator{ "." }) {
      if (!entry.is_regular_file()) { continue; }
      auto const extension{ entry.path().extension().string() };
      if (extension == ".cpp" || extension == ".hpp") {
        found.push_back(entry.path());
      }
    }
    return found;
  }

  auto Lines(fs::path const& file) -> std::vector<std::string>
  {
    std::vector<std::string> lines;
    std::ifstream in{ file };
    for (std::string line; std::getline(in, line); ) { lines.push_back(line); }
    return lines;
  }

  // every spelling the preprocessor accepts: indented, spaced, or quoted
  auto MentionsBoostInclude(std::string_view line) -> bool
  {
    auto const skip{ [&line](std::string_view chars) {
      line.remove_prefix(std::min(line.find_first_not_of(chars), line.size())); } };
    skip(" \t");
    if (!line.starts_with('#')) { return false; }
    line.remove_prefix(1);
    skip(" \t");
    if (!line.starts_with("include")) { return false; }
    line.remove_prefix(std::string_view{ "include" }.size());
    skip(" \t");
    return line.starts_with("<boost/") || line.starts_with("\"boost/");
  }

  TEST(TheAsioDoor, IsTheOnlyFileThatIncludesBoost)
  {
    auto const sources{ ModuleSources() };
    ASSERT_FALSE(sources.empty())
      << "the module's own sources were not found -- this suite reads them "
         "relative to its working directory, which buildutil sets to the "
         "module directory";

    for (auto const& file : sources) {
      if (file.filename() == DOOR) { continue; }
      for (auto const& line : Lines(file)) {
        EXPECT_FALSE(MentionsBoostInclude(line))
          << file.string() << " includes boost directly: " << line
          << "\n  Include \"oxbox/http/asio.hpp\" instead. It sets "
             "_WIN32_WINNT before asio is parsed, and every TU of this "
             "module has to agree on that -- see the header.";
      }
    }
  }

  TEST(TheAsioDoor, SetsTheWindowsVersionBeforeItIncludesAnything)
  {
    auto const door{ Lines(fs::path{ "." } / DOOR) };
    ASSERT_FALSE(door.empty()) << "asio.hpp not found beside this suite";

    auto const defines_it{ std::ranges::find_if(door, [](auto const& line) {
      return line.contains("#define _WIN32_WINNT"); }) };
    auto const includes_boost{ std::ranges::find_if(door,
      [](auto const& line) { return MentionsBoostInclude(line); }) };

    ASSERT_NE(defines_it, door.end()) << "the door no longer sets _WIN32_WINNT";
    ASSERT_NE(includes_boost, door.end()) << "the door includes no boost";
    EXPECT_LT(defines_it - door.begin(), includes_boost - door.begin())
      << "the version has to be set BEFORE the first boost header: after it, "
         "asio has already picked its API set from the SDK's default";
  }
}
