#include "oxbox/cli/unit.test/hello-world-cli.hpp"
//#include "_reflect/hello-world.cli.hpp"

#include <print>

namespace oxbox::cli::detail::hello_world_cli
{
  auto HelloWorld::operator() (int64_t                   value0,
                               array<float, 2>           value1to2,
                               tuple<string, int, float> value3to5,
                               RangeView<string_view>    value6toN 
                              ) const -> CliResult
  {
    return{ };
  }

  // The inline subcommand's whole implementation: no class, no state, and
  // the application's own option read straight off `this`.
  auto HelloWorldCli::say_greeting(string to_whom, optional<int> how_often)
    -> CliResult
  {
    auto const greeting{ greeting_language == "fr" ? "bonjour"sv : "hello"sv };
    for (auto said{ 0 }; said < how_often.value_or(1); ++said)
      print("{}, {}\n", greeting, to_whom);
    return{ };
  }

  auto HelloWorldCli::Initialize() -> CliResult
  {
    return{ };
  }
}