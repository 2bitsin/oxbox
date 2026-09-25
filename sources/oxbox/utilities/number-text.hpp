#pragma once
// A whole number written down, and a written number read back: the radix
// marker `std::from_chars` has no answer for.

#include "oxbox/utilities/hex.hpp"
#include "oxbox/utilities/short-types.hpp"
#include "oxbox/utilities/text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace oxbox::utilities::detail::number_text
{
  // The whole view or nothing: "12abc" read as 12 is a corrupt input silently
  // accepted, and an empty view is nullopt because absent and zero differ.
  template <std::integral _Number>
  inline constexpr auto WholeNumber(std::string_view input, int base = 10) -> std::optional<_Number>
  {
    if (input.empty())
      return std::nullopt;
    _Number value{ };
    auto const stop{ input.data() + input.size() };
    auto const parsed{ std::from_chars(input.data(), stop, value, base) };
    if (parsed.ec != std::errc{ } || parsed.ptr != stop)
      return std::nullopt;
    return value;
  }

  // The values are the bases, so `static_cast<int>(radix)` is the base.
  enum class Radix : U08 { BINARY = 2u, OCTAL = 8u, DECIMAL = 10u, HEX = 16u };

  // "let the text decide" is a parse-side answer, not a fifth base
  struct AsWrittenT { };
  inline constexpr AsWrittenT AsWritten{ };

  struct Marked
  {
    Radix            radix;
    std::string_view digits;
  };

  // nullopt when a marker is there but no digits follow it.
  inline constexpr auto RadixMarker(std::string_view text, Radix fallback) noexcept
    -> std::optional<Marked>
  {
    auto const two = [text](char lower, char upper) {
      return text.size() >= 2u && text[0] == '0'
          && (text[1] == lower || text[1] == upper);
    };

    if (two('x', 'X'))
      return Marked{ Radix::HEX, text.substr(2u) };
    if (two('o', 'O'))
      return Marked{ Radix::OCTAL, text.substr(2u) };
    // 'b' is a hex digit, so under a hex reading `0b…` is digits.
    if (fallback != Radix::HEX && two('b', 'B'))
      return Marked{ Radix::BINARY, text.substr(2u) };
    // The assembler suffix, last, so a marker at the front answers first.
    if (text.size() >= 2u && (text.back() == 'h' || text.back() == 'H'))
      return Marked{ Radix::HEX, text.substr(0u, text.size() - 1u) };
    return std::nullopt;
  }

  template <std::integral _Number>
  inline constexpr auto ParseNumber(std::string_view text,
                                    Radix fallback = Radix::DECIMAL)
    -> std::optional<_Number>
  {
    auto const marked{ RadixMarker(text, fallback) };
    if (!marked)
      return WholeNumber<_Number>(text, static_cast<int>(fallback));
    if (marked->digits.empty())
      return std::nullopt;
    // `0x-1` would otherwise reach from_chars, which takes it and answers -1;
    // from_chars refuses '+' at every base, so this does too.
    auto const signed_at = [](std::string_view where) {
      return where.front() == '-' || where.front() == '+';
    };
    if (signed_at(text) || signed_at(marked->digits))
      return std::nullopt;
    return WholeNumber<_Number>(marked->digits, static_cast<int>(marked->radix));
  }

  // 'b' is a hex letter, so `0b1010` would otherwise read as 0x0B1010.
  template <std::integral _Number>
  inline constexpr auto ParseNumber(std::string_view text, AsWrittenT)
    -> std::optional<_Number>
  {
    if (RadixMarker(text, Radix::DECIMAL))
      return ParseNumber<_Number>(text, Radix::DECIMAL);

    auto const looks_hex{ std::ranges::any_of(text, [](char letter) {
      return (letter >= 'a' && letter <= 'f') || (letter >= 'A' && letter <= 'F');
    }) };
    return ParseNumber<_Number>(text, looks_hex ? Radix::HEX : Radix::DECIMAL);
  }

  template <std::integral _Number, std::size_t _Count>
  inline constexpr auto ParseNumbers(std::string_view text, char separator,
                                     Radix fallback = Radix::DECIMAL)
    -> std::optional<std::array<_Number, _Count>>
  {
    auto fields{ text | std::views::split(separator) };
    if (std::ranges::distance(fields) != static_cast<std::ptrdiff_t>(_Count))
      return std::nullopt;
    std::array<_Number, _Count> values{ };
    for (auto&& [slot, field] : std::views::zip(values, fields))
    {
      auto const value{ ParseNumber<_Number>(std::string_view{ field }, fallback) };
      if (!value)
        return std::nullopt;
      slot = *value;
    }
    return values;
  }

  // The first whitespace-delimited token after the first marker; an empty marker is nullopt.
  template <std::integral _Number>
  inline constexpr auto ParseNumberAfter(std::string_view line, std::string_view marker,
                                         Radix fallback = Radix::DECIMAL) -> std::optional<_Number>
  {
    if (marker.empty())
      return std::nullopt;
    auto const at{ line.find(marker) };
    if (at == std::string_view::npos)
      return std::nullopt;
    auto const remainder{ Trimmed(line.substr(at + marker.size())) };
    return ParseNumber<_Number>(remainder.substr(0u, remainder.find_first_of(WHITESPACE)),
                                fallback);
  }

  template <std::unsigned_integral _Number>
  inline constexpr auto NumberDigits(_Number magnitude, Radix radix, std::size_t width,
                                     HexCase casing) -> std::string
  {
    auto const base{ static_cast<_Number>(radix) };
    auto const digits{ HexDigits(casing) };
    // Least significant digit first, then reversed; zero still writes a '0'.
    std::string out;
    do
    {
      out.push_back(digits[static_cast<std::size_t>(magnitude % base)]);
      magnitude = static_cast<_Number>(magnitude / base);
    }
    while (magnitude != _Number{ 0 });
    out.resize(std::max(out.size(), width), '0');
    std::ranges::reverse(out);
    return out;
  }

  template <std::integral _Number>
  inline constexpr auto IsNegative(_Number value) noexcept -> bool
  {
    if constexpr (std::is_signed_v<_Number>)
      return value < _Number{ 0 };
    return false;
  }

  // Decimal writes a minus sign; the power-of-two radices write the value's
  // two's-complement bit pattern. `width` is a minimum, never a truncation.
  template <std::integral _Number>
  inline constexpr auto FormatNumber(_Number value, Radix radix,
                                     std::size_t width = 0u,
                                     std::string_view prefix = { },
                                     HexCase casing = HexCase::UPPER) -> std::string
  {
    using Unsigned = std::make_unsigned_t<_Number>;
    auto const negative{ radix == Radix::DECIMAL && IsNegative(value) };
    // Negating the most negative value of a signed type is undefined.
    auto const magnitude{ negative ? static_cast<Unsigned>(Unsigned{ 0 } - static_cast<Unsigned>(value))
                                   : static_cast<Unsigned>(value) };
    auto const out{ NumberDigits(magnitude, radix, width, casing) };

    std::string composed;
    composed.reserve(out.size() + prefix.size() + (negative ? 1u : 0u));
    if (negative)
      composed.push_back('-');
    composed.append(prefix);
    composed.append(out);
    return composed;
  }

  // Hex at a fixed width, defaulting to the type's own.
  template <std::integral _Number>
  inline constexpr auto HexText(_Number value,
                                std::size_t digits = 2u * sizeof(_Number),
                                HexCase casing = HexCase::UPPER,
                                std::string_view prefix = { }) -> std::string
  {
    return FormatNumber(value, Radix::HEX, digits, prefix, casing);
  }
}

namespace oxbox::utilities
{
  using detail::number_text::AsWritten;
  using detail::number_text::AsWrittenT;
  using detail::number_text::FormatNumber;
  using detail::number_text::HexText;
  using detail::number_text::Marked;
  using detail::number_text::ParseNumber;
  using detail::number_text::ParseNumberAfter;
  using detail::number_text::ParseNumbers;
  using detail::number_text::Radix;
  using detail::number_text::RadixMarker;
}
