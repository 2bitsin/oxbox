// Refused on purpose: `d` is an integer presentation and the argument is text.
#include "oxbox/utilities/exception.hpp"

#include "oxbox/utilities/hash.hpp"

#include <stdexcept>
#include <string>

using namespace oxbox::utilities::literals;

using Refused = oxbox::utilities::Exception<"Refused"_hash, std::runtime_error, "port {:d}", std::string>;

auto main() -> int { return Refused{ "http" }.what()[0]; }
