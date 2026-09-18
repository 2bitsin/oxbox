#pragma once
// The seam a body crosses to become the encoding a caller asked for.

#include "oxbox/utilities/unicode.hpp"

#include <bit>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace oxbox::http::detail::delivery
{
  using utilities::Encoding;

  // The encoding, and the byte order the name itself states (RFC 9110
  // 8.3.2); a nullopt order means the name stated none.
  struct Charset {
    Encoding                   encoding { };
    std::optional<std::endian> order    { };
  };

  [[nodiscard]] auto CharsetOf(std::string_view name) -> std::optional<Charset>;

  class Delivery {
  public:
    // `required` is nullopt for a caller wanting octets. Throws
    // TransportError on a required/declared pair this client cannot honour.
    [[nodiscard]] static auto For(std::optional<Encoding>         required,
                                  std::optional<std::string_view> declared,
                                  std::string_view                url) -> Delivery;

    // The answer views the input or this object, until the next call.
    [[nodiscard]] auto Deliver(std::span<std::byte const> bytes)
        -> std::span<std::byte const>;

    [[nodiscard]] auto Flush() -> std::span<std::byte const>;

  private:
    Delivery() = default;
  };

  [[nodiscard]] auto CharsetName(Encoding encoding) noexcept -> std::string_view;
}

namespace oxbox::http
{
  using detail::delivery::Charset;
  using detail::delivery::CharsetName;
  using detail::delivery::CharsetOf;
  using detail::delivery::Delivery;
}
