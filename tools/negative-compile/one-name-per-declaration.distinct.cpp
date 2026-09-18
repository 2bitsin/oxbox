// The control, and it must compile: the same shape as
// one-name-per-declaration.collides.cpp with the collision taken out by one
// character, so it is a near miss on purpose -- the check compares through
// the transform. Without it a broken include path would read as a guard.

#include "oxbox/cli/parse.hpp"

#include <string_view>
#include <string>
#include <vector>

namespace oxbox::cli::negative_compile
{
  struct Distinct : Command
  {
    friend constexpr auto reflect_scheme(Distinct*);

    std::string content_type { };   /* --content-type, from the identifier */
    std::string kinds        { };   /* --content-types, from the label */
  };

  constexpr auto reflect_scheme(Distinct*)
  {
    using T = Distinct;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"content_type", &T::content_type,
                               "from the identifier", false>,
      ::reflect::member_scheme<"kinds",        &T::kinds,
                               "from the label", false, "content-types">>{ };
  }

  auto Accepted() -> Outcome
  {
    return ScanOptions<Distinct>({ });
  }
}
