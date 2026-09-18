#include "oxbox/http/delivery.hpp"

#include "oxbox/http/error.hpp"
#include "oxbox/utilities/text.hpp"

#include <bit>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace oxbox::http::detail::delivery
{
  using namespace std::string_view_literals;

  namespace
  {
    // IANA's names, plus the one alias servers really send ("latin1").
    auto CharsetTable() -> std::unordered_map<std::string_view, Charset> const&
    {
      // every name CharsetName emits is also a row here, or a server echoing
      // one back would slip the refusal below; an absent name is not an error
      static std::unordered_map<std::string_view, Charset> const NAMES{
          { "utf-8"sv,           { Encoding::UTF8,  std::nullopt        } },
          { "us-ascii"sv,        { Encoding::UCS1,  std::nullopt        } },
          { "iso-8859-1"sv,      { Encoding::UCS1,  std::nullopt        } },
          { "latin1"sv,          { Encoding::UCS1,  std::nullopt        } },
          { "utf-16"sv,          { Encoding::UTF16, std::nullopt        } },
          { "utf-16le"sv,        { Encoding::UTF16, std::endian::little } },
          { "utf-16be"sv,        { Encoding::UTF16, std::endian::big    } },
          { "utf-32"sv,          { Encoding::UCS4,  std::nullopt        } },
          { "utf-32le"sv,        { Encoding::UCS4,  std::endian::little } },
          { "utf-32be"sv,        { Encoding::UCS4,  std::endian::big    } },
          { "ucs-2"sv,           { Encoding::UCS2,  std::nullopt        } },
          { "iso-10646-ucs-2"sv, { Encoding::UCS2,  std::nullopt        } },
          { "ucs-4"sv,           { Encoding::UCS4,  std::nullopt        } },
          { "iso-10646-ucs-4"sv, { Encoding::UCS4,  std::nullopt        } } };
      return NAMES;
    }

    // The wide encodings are the ones whose code unit is not an octet, so no
    // run of their bytes is the same text read one byte at a time.
    auto Wide(Encoding encoding) noexcept -> bool
    {
      switch (encoding) {
        case Encoding::UCS2:
        case Encoding::UTF16:
        case Encoding::UCS4:  return true;
        case Encoding::UCS1:
        case Encoding::UTF8:  return false;
      }
      std::unreachable();  // Encoding is closed and every enumerator is above
    }
  }

  auto CharsetOf(std::string_view name) -> std::optional<Charset>
  {
    auto const& table{ CharsetTable() };
    auto const  found{ table.find(utilities::Lowered(utilities::Trimmed(name))) };
    if (found == table.end())
      return std::nullopt;
    return found->second;
  }

  auto Delivery::For(std::optional<Encoding>         required,
                     std::optional<std::string_view> declared,
                     std::string_view                url) -> Delivery
  {
    // A caller that promised nothing about the bytes is owed nothing here.
    if (!required)
      return Delivery{ };

    // Answered before the declaration is read: nothing arriving is already a
    // wide encoding, so every path would otherwise pass bytes off as one.
    if (Wide(*required))
      throw TransportError{ std::format(
          "http delivery: {} asked for text as {}, and delivering a body as it "
          "is not implemented yet", url, CharsetName(*required)) };

    if (!declared)
      return Delivery{ };

    auto const charset{ CharsetOf(*declared) };
    if (!charset || !Wide(charset->encoding))
      return Delivery{ };

    throw TransportError{ std::format(
        "http delivery: {} declared 'charset={}', and transcoding it to {} is "
        "not implemented yet", url, *declared, CharsetName(*required)) };
  }

  auto Delivery::Deliver(std::span<std::byte const> bytes)
      -> std::span<std::byte const>
  {
    return bytes;
  }

  auto Delivery::Flush() -> std::span<std::byte const>
  {
    return { };
  }

  auto CharsetName(Encoding encoding) noexcept -> std::string_view
  {
    switch (encoding) {
      case Encoding::UCS1:  return "us-ascii"sv;
      case Encoding::UTF8:  return "utf-8"sv;
      case Encoding::UCS2:  return "iso-10646-ucs-2"sv;
      case Encoding::UTF16: return "utf-16"sv;
      case Encoding::UCS4:  return "utf-32"sv;
    }
    std::unreachable();  // Encoding is closed and every enumerator is above
  }
}
