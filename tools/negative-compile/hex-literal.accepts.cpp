// The control, and it must compile. It is also the only place both hex
// spellings compile together, which cl 19.51 could not while they shared a name.
#include "oxbox/utilities/hex.hpp"

#include <array>
#include <cstddef>

using namespace oxbox::utilities::literals;

constexpr auto NUMERIC{ 0xDEAD'BEEF_hex };
constexpr auto TEXT   { oxbox::utilities::HexBytes<"4d 5a">() };
constexpr auto ODD    { 0xABC_hex };

static_assert(NUMERIC == std::array<std::byte, 4u>{
  std::byte{ 0xDEu }, std::byte{ 0xADu }, std::byte{ 0xBEu }, std::byte{ 0xEFu } });
static_assert(TEXT.size() == 2u);
static_assert(ODD == std::array<std::byte, 2u>{ std::byte{ 0x0Au }, std::byte{ 0xBCu } });

auto main() -> int { return static_cast<int>(NUMERIC.size() + TEXT.size()); }
