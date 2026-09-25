// The control, and it must compile: every argument has its field and fits it.
#include "oxbox/utilities/exception.hpp"

#include "oxbox/utilities/hash.hpp"

#include <stdexcept>
#include <string>

using namespace oxbox::utilities::literals;

using Refused = oxbox::utilities::Exception<"Refused"_hash, std::runtime_error, "port {:d} of {}", int, std::string>;

auto main() -> int { return Refused{ 3, "peer" }.what()[0] == 'p' ? 0 : 1; }
