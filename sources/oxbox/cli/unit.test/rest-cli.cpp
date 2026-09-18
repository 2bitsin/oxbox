#include "oxbox/cli/unit.test/rest-cli.hpp"

namespace oxbox::cli::detail::rest_cli
{
  auto Undocumented::foo(std::string) -> CliResult
  {
    return { };
  }

  auto Wrapper::operator() (std::optional<std::string>) -> CliResult
  {
    return{ };
  }

  auto Borrowing::operator() () -> CliResult { return{ }; }

  auto Joining::operator() () -> CliResult { return{ }; }

  auto Plain::operator() (std::vector<std::string>) -> CliResult
  {
    return{ };
  }

  auto Leaf::operator() () -> CliResult { return{ }; }

  auto NearMiss::operator() () -> CliResult { return{ }; }

  auto Layered::operator() () -> CliResult { return{ }; }

  auto Painter::operator() (std::string) -> CliResult { return{ }; }

  auto Renamed::enumerate() -> Leaf& { return Command::Get<Leaf>(); }

  auto Renamed::greet(std::string) -> CliResult { return{ }; }
}
