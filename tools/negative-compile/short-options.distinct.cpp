#include "oxbox/cli/parse.hpp"

namespace short_negative
{
  struct Options : oxbox::cli::Command
  {
    bool verbose{ };
    bool output{ };
  };

  constexpr auto reflect_scheme(Options*)
  {
    using T = Options;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"verbose", &T::verbose, "", false, "",
        ::reflect::tag_list<"-v">>,
      ::reflect::member_scheme<"output", &T::output, "", false, "",
        ::reflect::tag_list<"-o", "unrelated">>>{ };
  }

  auto Check() -> oxbox::cli::Outcome
  {
    return oxbox::cli::ScanOptions<Options>({ });
  }
}
