#pragma once
// Bytes as hex text and back, and the nibble table both directions use.
// Lifted from bossdeux (utilities/strings.hpp, byte_string.hpp), xoctet
// (xoctet-document/hex-text.cpp) and qwenlab (lab-final's hex.cpp).

#include "oxbox/utilities/fixed-string.hpp"
#include "oxbox/utilities/short-types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oxbox::utilities::detail::hex
{
  using namespace std::string_view_literals;

  enum class HexCase : U08 { LOWER, UPPER };

  // What a person's copy-and-paste brings along.
  inline constexpr auto HEX_SEPARATORS{ " -,:"sv };

  inline constexpr auto HEX_DIGITS_LOWER{ "0123456789abcdef"sv };
  inline constexpr auto HEX_DIGITS_UPPER{ "0123456789ABCDEF"sv };

  inline constexpr auto HexDigits(HexCase casing) noexcept -> std::string_view
  {
    return casing == HexCase::UPPER ? HEX_DIGITS_UPPER : HEX_DIGITS_LOWER;
  }

  // nullopt for anything that is not a hex digit.
  inline constexpr auto HexDigitValue(char digit) noexcept -> std::optional<U08>
  {
    if (digit >= '0' && digit <= '9') return static_cast<U08>(digit - '0');
    if (digit >= 'a' && digit <= 'f') return static_cast<U08>(digit - 'a' + 10);
    if (digit >= 'A' && digit <= 'F') return static_cast<U08>(digit - 'A' + 10);
    return std::nullopt;
  }

  inline constexpr auto IsHexDigit(char digit) noexcept -> bool
  {
    return HexDigitValue(digit).has_value();
  }

  // ---- bytes -> text --------------------------------------------------

  // Separators fall between bytes, so an empty input has none.
  inline constexpr auto HexTextSize(std::size_t count,
                                    std::size_t separator_size = 0u) noexcept
    -> std::size_t
  {
    return count == 0u ? 0u : count * 2u + (count - 1u) * separator_size;
  }

  // The answer is the prefix written: a buffer that cannot hold everything
  // gets whole bytes and stops.
  inline constexpr auto ToHexInto(std::span<char> into, Bytes data,
                                  HexCase casing = HexCase::LOWER,
                                  std::string_view separator = { }) noexcept
    -> std::string_view
  {
    auto const digits{ HexDigits(casing) };
    std::size_t at{ 0u };
    for (std::size_t index{ 0u }; index < data.size(); ++index)
    {
      auto const step{ (index == 0u ? 0u : separator.size()) + 2u };
      if (at + step > into.size())
        break;
      if (index != 0u)
      {
        std::ranges::copy(separator, into.begin() + static_cast<std::ptrdiff_t>(at));
        at += separator.size();
      }
      auto const octet{ std::to_integer<U08>(data[index]) };
      into[at++] = digits[octet >> 4u];
      into[at++] = digits[octet & 0x0Fu];
    }
    return std::string_view{ into.data(), at };
  }

  inline constexpr auto ToHex(Bytes data, HexCase casing = HexCase::LOWER,
                              std::string_view separator = { }) -> std::string
  {
    std::string out(HexTextSize(data.size(), separator.size()), '\0');
    static_cast<void>(ToHexInto(std::span{ out }, data, casing, separator));
    return out;
  }

  // ---- text -> bytes --------------------------------------------------

  // The facts a caller needs to compose a sentence about a refusal; the
  // wording belongs to whoever has the status bar.
  struct HexFault
  {
    enum class Kind : U08
    {
      STRAY_CHARACTER,   // `character` at `position` is neither digit nor separator
      HALF_BYTE          // `digits` is odd; position/character name the last digit
    };

    Kind        kind;
    std::size_t position;    // index into the text, not into the digits
    char        character;
    std::size_t digits;      // hex digits counted up to and including the fault
  };

  // nullopt means the text parses.
  inline constexpr auto HexFaultIn(std::string_view text,
                                   std::string_view separators = HEX_SEPARATORS)
    noexcept -> std::optional<HexFault>
  {
    std::size_t digits{ 0u };
    std::size_t last{ 0u };
    char        last_digit{ '\0' };
    for (std::size_t at{ 0u }; at < text.size(); ++at)
    {
      char const letter{ text[at] };
      if (separators.contains(letter))
        continue;
      if (!IsHexDigit(letter))
        return HexFault{ HexFault::Kind::STRAY_CHARACTER, at, letter, digits };
      ++digits;
      last = at;
      last_digit = letter;
    }
    if (digits % 2u != 0u)
      return HexFault{ HexFault::Kind::HALF_BYTE, last, last_digit, digits };
    return std::nullopt;
  }

  // HexFaultIn read as a yes or no, so a count and a diagnosis cannot differ.
  inline constexpr auto HexByteCount(std::string_view text,
                                     std::string_view separators = HEX_SEPARATORS)
    noexcept -> std::optional<std::size_t>
  {
    auto const fault{ HexFaultIn(text, separators) };
    if (fault)
      return std::nullopt;
    return static_cast<std::size_t>(
      std::ranges::count_if(text, [separators](char letter) {
        return !separators.contains(letter); })) / 2u;
  }

  // On refusal the bytes already decoded stay in `into`: read the answer,
  // which is the span written, and not the buffer.
  inline constexpr auto BytesFromHexInto(WritableBytes into, std::string_view text,
                                         std::string_view separators = HEX_SEPARATORS)
    noexcept -> std::optional<WritableBytes>
  {
    std::optional<U08> high;
    std::size_t at{ 0u };
    for (char const letter : text)
    {
      if (separators.contains(letter))
        continue;
      auto const nibble{ HexDigitValue(letter) };
      if (!nibble)
        return std::nullopt;
      if (!high)
      {
        high = nibble;
        continue;
      }
      if (at == into.size())
        return std::nullopt;
      into[at++] = std::byte{ static_cast<U08>((*high << 4u) | *nibble) };
      high.reset();
    }
    if (high)
      return std::nullopt;
    return into.first(at);
  }

  // Empty text is an empty sequence and not a refusal.
  inline constexpr auto BytesFromHex(std::string_view text,
                                     std::string_view separators = HEX_SEPARATORS)
    -> std::optional<std::vector<std::byte>>
  {
    auto const count{ HexByteCount(text, separators) };
    if (!count)
      return std::nullopt;
    std::vector<std::byte> out(*count);
    static_cast<void>(BytesFromHexInto(WritableBytes{ out }, text, separators));
    return out;
  }

  // ---- the fixtures ---------------------------------------------------

  // Hex text as an array at compile time; consteval, so a typo in a fixture
  // stops the build. A function and not a `_hex` literal because cl 19.51
  // cannot pick between a string-literal operator template and a `char...`
  // raw-literal operator template of one name.
  template <FixedString _TEXT>
  consteval auto HexBytes()
  {
    static_assert(HexByteCount(_TEXT.view()).has_value(),
                  "hex fixture: a stray character, or an odd number of digits");
    constexpr auto COUNT{ HexByteCount(_TEXT.view()).value_or(0u) };
    std::array<std::byte, COUNT> out{ };
    static_cast<void>(BytesFromHexInto(WritableBytes{ out }, _TEXT.view()));
    return out;
  }

  // A variable template and not a local: a constexpr array inside a consteval
  // function has automatic storage, so a string_view over one is not a
  // constant expression. C++ says the digit separator ' is not part of the
  // number, so it is dropped here rather than reaching the nibble table.
  template <char... _DIGITS>
  inline constexpr auto HEX_TOKEN = []
  {
    std::array<char, sizeof...(_DIGITS)> out{ };
    std::size_t kept{ 0u };
    for (char const letter : { _DIGITS... })
      if (letter != '\'')
        out[kept++] = letter;
    return std::pair<std::array<char, sizeof...(_DIGITS)>, std::size_t>{ out, kept };
  }();

  template <char... _DIGITS>
  inline constexpr std::string_view HEX_SPELLING{ HEX_TOKEN<_DIGITS...>.first.data(),
                                                  HEX_TOKEN<_DIGITS...>.second };

  inline namespace literals
  {
    // 'b' is a hex digit, so `0b1010_hex` would silently mean 0x0B10 and
    // `0755_hex` 0x0755: a leading 0 that is not 0x is refused. An odd number
    // of digits pads at the front, so `0xABC_hex` is {0x0A, 0xBC}.
    template <char... _DIGITS>
    consteval auto operator""_hex()
    {
      constexpr auto SPELLING{ HEX_SPELLING<_DIGITS...> };
      constexpr bool PREFIXED{ SPELLING.size() >= 2u && SPELLING[0] == '0'
                            && (SPELLING[1] == 'x' || SPELLING[1] == 'X') };
      static_assert(PREFIXED || SPELLING.size() < 2u || SPELLING[0] != '0',
                    "hex literal: a leading 0 that is not 0x is not hex "
                    "(0b1010 and 0755 would read as hex digits)");
      constexpr auto DIGITS{ PREFIXED ? SPELLING.substr(2u) : SPELLING };
      static_assert(!DIGITS.empty(), "hex literal: no digits");
      static_assert(std::ranges::all_of(DIGITS, [](char letter) {
                      return IsHexDigit(letter); }),
                    "hex literal: a character that is not a hex digit "
                    "(a digit separator ' is allowed and ignored; "
                    "an exponent or a suffix is not)");
      constexpr std::size_t COUNT{ (DIGITS.size() + 1u) / 2u };
      std::array<std::byte, COUNT> out{ };
      std::size_t at{ 0u };
      std::size_t index{ 0u };
      if (DIGITS.size() % 2u != 0u)
        out[at++] = std::byte{ HexDigitValue(DIGITS[index++]).value_or(0u) };
      while (index < DIGITS.size())
      {
        auto const high{ HexDigitValue(DIGITS[index++]).value_or(0u) };
        auto const low { HexDigitValue(DIGITS[index++]).value_or(0u) };
        out[at++] = std::byte{ static_cast<U08>((high << 4u) | low) };
      }
      return out;
    }
  }
}

namespace oxbox::utilities
{
  using detail::hex::BytesFromHex;
  using detail::hex::BytesFromHexInto;
  using detail::hex::HEX_DIGITS_LOWER;
  using detail::hex::HEX_DIGITS_UPPER;
  using detail::hex::HEX_SEPARATORS;
  using detail::hex::HexByteCount;
  using detail::hex::HexFault;
  using detail::hex::HexFaultIn;
  using detail::hex::HexBytes;
  using detail::hex::HexCase;
  using detail::hex::HexDigitValue;
  using detail::hex::HexDigits;
  using detail::hex::HexTextSize;
  using detail::hex::IsHexDigit;
  using detail::hex::ToHex;
  using detail::hex::ToHexInto;

  inline namespace literals
  {
    using detail::hex::literals::operator""_hex;
  }
}
