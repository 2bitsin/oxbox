// Must not compile: `content_type` and the label on `kinds` both spell
// --content-type.

#include "oxbox/cli/parse.hpp"

#include <string_view>
#include <string>
#include <vector>

namespace oxbox::cli::negative_compile
{
  struct Divided : Command
  {
    friend constexpr auto reflect_scheme(Divided*);

    std::string content_type { };   /* --content-type, from the identifier */
    std::string kinds        { };   /* --content-type, from the label */
  };

  constexpr auto reflect_scheme(Divided*)
  {
    using T = Divided;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"content_type", &T::content_type,
                               "from the identifier", false>,
      ::reflect::member_scheme<"kinds",        &T::kinds,
                               "from the label", false, "content-type">>{ };
  }

  // The scan form needs no object, so this never has to link.
  auto Refused() -> Outcome
  {
    return ScanOptions<Divided>({ });
  }
}
