#pragma once

#include "oxbox/cli/command.hpp"

#include <string>

namespace oxbox::cli::short_test
{
  struct Options : Command
  {
    friend constexpr auto reflect_scheme(Options*);

    bool verbose{ } _Meta("-v"); /* say what is being done */
    std::string output{ } _Meta("-o", "other-tag"); /* destination */
  };

  struct Claimed : Command
  {
    friend constexpr auto reflect_scheme(Claimed*);

    bool hush{ } _Meta("-h"); /* be quiet */
  };

  struct OwnHelp : Command
  {
    friend constexpr auto reflect_scheme(OwnHelp*);

    bool help{ }; /* application help */
  };
}
