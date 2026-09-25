// Refused on purpose: the alias is only named by a handler and never constructed.
#include "oxbox/utilities/exception.hpp"

#include "oxbox/utilities/hash.hpp"

#include <stdexcept>

using namespace oxbox::utilities::literals;

using Refused = oxbox::utilities::Exception<"Refused"_hash, std::runtime_error, "port {} of {}", int>;

auto main() -> int
{
  try
  {
    return 0;
  }
  catch (Refused const&)
  {
    return 1;
  }
}
