#pragma once

// The one backslash rule for an option's value text, and splitting on the
// separators the writer did not escape.

#include <cstddef>
#include <string_view>
#include <string>
#include <vector>

namespace oxbox::cli::detail::escaping
{
  // A backslash makes the next character literal; a trailing one is itself.
  constexpr auto FindUnescaped(std::string_view text, char wanted) noexcept
    -> std::size_t
  {
    for (std::size_t at{ 0u }; at < text.size(); ++at) {
      if (text[at] == '\\') { ++at; continue; }
      if (text[at] == wanted) return at;
    }
    return std::string_view::npos;
  }

  // Pieces keep their backslashes: the caller may have another level to split.
  inline auto SplitUnescaped(std::string_view text, char separator)
    -> std::vector<std::string_view>
  {
    std::vector<std::string_view> pieces;
    for (auto found{ FindUnescaped(text, separator) };
         found != std::string_view::npos;
         found = FindUnescaped(text, separator)) {
      pieces.push_back(text.substr(0u, found));
      text = text.substr(found + 1u);
    }
    pieces.push_back(text);
    return pieces;
  }

  // Materialised because the meaning is not a substring of what was typed.
  inline auto Unescaped(std::string_view text) -> std::string
  {
    std::string out;
    out.reserve(text.size());
    for (std::size_t at{ 0u }; at < text.size(); ++at) {
      if (text[at] == '\\' && at + 1u < text.size()) ++at;
      out += text[at];
    }
    return out;
  }
}
