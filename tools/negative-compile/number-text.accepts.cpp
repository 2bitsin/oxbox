#include "oxbox/utilities/number-text.hpp"

#include <array>
#include <optional>

using namespace oxbox::utilities;

static_assert(detail::number_text::WholeNumber<int>("42") == 42);
static_assert(ParseNumber<int>("0x2a") == 42);
static_assert(ParseNumber<int>("2a", AsWritten) == 42);
static_assert(ParseNumbers<int, 2>("1:2", ':') == std::array{ 1, 2 });
static_assert(detail::number_text::NumberDigits(42u, Radix::HEX, 4u, HexCase::UPPER) == "002A");
static_assert(FormatNumber(-42, Radix::DECIMAL, 4u, "#") == "-#0042");
static_assert(HexText(U08{ 42 }) == "2A");

auto Accepted() -> std::optional<int>
{
  return ParseNumberAfter<int>("rate=1", "rate=");
}
