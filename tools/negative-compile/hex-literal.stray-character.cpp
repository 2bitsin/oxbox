// Refused on purpose: `HexBytes<"4d 5g">()` would be a shorter needle than typed.
#include "oxbox/utilities/hex.hpp"

constexpr auto REFUSED{ oxbox::utilities::HexBytes<"4d 5g">() };

auto main() -> int { return static_cast<int>(REFUSED.size()); }
