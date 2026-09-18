#pragma once

#include "oxbox/cli/main.hpp"

namespace oxbox::cli::detail::print_version_cli
{
  using namespace std;
  
  struct PrintVersion: Command {
    friend constexpr auto reflect_scheme(PrintVersion*);

    // No parameters for this function, so should error if anything more is specified
    auto operator() () const -> CliResult;

    PrintVersion() = default;

    PrintVersion(PrintVersion const&) = default;
    PrintVersion(PrintVersion &&) noexcept = default;
    PrintVersion& operator = (PrintVersion const&) = default;
    PrintVersion& operator = (PrintVersion &&) noexcept = default;
    ~PrintVersion() noexcept = default;


  private:
    string _version{ "0.0.1-alpha" }; // Notice how this is a private variable, and private variables
                     // do not get turned into cli options, they remain private.
  };
}

namespace oxbox::cli
{
  using detail::print_version_cli::PrintVersion;
}