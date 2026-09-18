#pragma once
// An absolute http(s) url as a value. Spec: RFC 3986 3, RFC 9110 4.2 and 7.2.

#include <cstdint>
#include <string>
#include <string_view>

namespace oxbox::http::detail::url
{
  enum class Scheme: std::uint8_t { HTTP, HTTPS };

  // `port` is always explicit, `host` is bare (an IPv6 literal's brackets
  // are url syntax) and `target` is origin-form (RFC 9112 3.2.1).
  struct Endpoint {
    Scheme      scheme { Scheme::HTTPS };
    std::string host   {              };
    std::string port   {              };
    std::string target {              };
  };

  // Throws std::invalid_argument for a missing or unknown scheme, or no host.
  auto SplitUrl(std::string_view url) -> Endpoint;

  auto Authority(Endpoint const& endpoint) -> std::string;
}

namespace oxbox::http
{
  using detail::url::Authority;
  using detail::url::Endpoint;
  using detail::url::Scheme;
  using detail::url::SplitUrl;
}
