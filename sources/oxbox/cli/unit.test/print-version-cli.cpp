#include "oxbox/cli/unit.test/print-version-cli.hpp"
//#include "_reflect/print-version.cli.hpp"

#include <print>

namespace oxbox::cli::detail::print_version_cli 
{
  auto PrintVersion::operator()() const -> CliResult {
    std::println("{}", _version);
    return{ };
  }

}