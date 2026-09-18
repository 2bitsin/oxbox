// Refused on purpose: 'b' is a hex digit, so `0b1010_hex` would mean 0x0B 0x10.
#include "oxbox/utilities/hex.hpp"

using namespace oxbox::utilities::literals;

constexpr auto REFUSED{ 0b1010_hex };

auto main() -> int { return static_cast<int>(REFUSED.size()); }
