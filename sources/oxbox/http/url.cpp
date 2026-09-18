#include "oxbox/http/url.hpp"

#include "oxbox/utilities/text.hpp"

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace oxbox::http::detail::url
{
  namespace
  {
    constexpr std::string_view SCHEME_SEPARATOR{ "://" };

    // Scheme token -> what it names. A keyed lookup, so a map: another
    // scheme is a row here, never a branch somewhere else.
    auto const SCHEME_BY_NAME = std::unordered_map<std::string_view, Scheme>{
        { "http",  Scheme::HTTP  },
        { "https", Scheme::HTTPS } };

    // The port a url of this scheme means when it names none. Stated
    // here and nowhere else, so the two spellings of "https means 443"
    // cannot drift apart.
    auto DefaultPort(Scheme scheme) noexcept -> std::string_view
    {
      switch (scheme) {
        case Scheme::HTTP:  return "80";
        case Scheme::HTTPS: return "443";
      }
      std::unreachable();  // Scheme is closed and every enumerator is above
    }
  }

  auto SplitUrl(std::string_view url) -> Endpoint
  {
    auto const separator{ url.find(SCHEME_SEPARATOR) };
    if (separator == std::string_view::npos)
      throw std::invalid_argument{ std::format(
          "http url: '{:.120}' is not an absolute url", url) };

    auto const known{ SCHEME_BY_NAME.find(
        utilities::Lowered(url.substr(0u, separator))) };
    if (known == SCHEME_BY_NAME.end())
      throw std::invalid_argument{ std::format(
          "http url: '{:.40}' is not a scheme this client speaks (http, https)",
          url.substr(0u, separator)) };

    auto const rest{ url.substr(separator + SCHEME_SEPARATOR.size()) };
    auto const path{ rest.find('/') };
    auto const authority{ rest.substr(0u, path) };

    // An IPv6 literal carries its own colons inside brackets, so the
    // port separator is only a colon after the closing bracket — and the
    // brackets themselves are url syntax the resolver must not see.
    auto const closing{ authority.starts_with('[') ? authority.find(']')
                                                   : std::string_view::npos };
    auto const port_mark{ authority.find(
        ':', closing == std::string_view::npos ? 0u : closing) };
    auto const host{ closing == std::string_view::npos
                         ? authority.substr(0u, port_mark)
                         : authority.substr(1u, closing - 1u) };
    if (host.empty())
      throw std::invalid_argument{ std::format(
          "http url: '{:.120}' names no host", url) };

    return Endpoint{
        .scheme = known->second,
        .host   = std::string{ host },
        .port   = port_mark == std::string_view::npos
                      ? std::string{ DefaultPort(known->second) }
                      : std::string{ authority.substr(port_mark + 1u) },
        .target = path == std::string_view::npos
                      ? std::string{ "/" }
                      : std::string{ rest.substr(path) } };
  }

  auto Authority(Endpoint const& endpoint) -> std::string
  {
    auto const literal{ endpoint.host.contains(':')
                            ? std::format("[{}]", endpoint.host)
                            : endpoint.host };
    if (endpoint.port == DefaultPort(endpoint.scheme))
      return literal;
    return std::format("{}:{}", literal, endpoint.port);
  }
}
