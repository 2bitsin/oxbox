// Refused on purpose: the format has two fields and the alias one argument.
#include "oxbox/utilities/exception.hpp"

#include "oxbox/utilities/hash.hpp"

#include <stdexcept>

using namespace oxbox::utilities::literals;

using Refused = oxbox::utilities::Exception<"Refused"_hash, std::runtime_error, "port {} of {}", int>;

auto main() -> int { return Refused{ 3 }.what()[0]; }
