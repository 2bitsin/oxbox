#include "oxbox/http/router.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace oxbox::http::detail::router
{
  using namespace std::string_view_literals;

  namespace bh = boost::beast::http;

  namespace
  {
    constexpr auto SEPARATOR{ ", "sv };
  }

  auto Router::Add(Route route) -> void
  {
    routes_.push_back(std::move(route));
  }

  auto Router::Match(Method method, std::string_view path) const
      -> Route const*
  {
    for (Route const& route : routes_)
      if (route.method == method && route.path == path)
        return &route;
    return nullptr;
  }

  auto Router::Knows(std::string_view path) const -> bool
  {
    for (Route const& route : routes_)
      if (route.path == path)
        return true;
    return false;
  }

  auto Router::Allowed(std::string_view path) const -> std::string
  {
    std::string named{ };
    for (Route const& route : routes_) {
      if (route.path != path)
        continue;
      if (!named.empty())
        named += SEPARATOR;
      auto const name{ bh::to_string(route.method) };
      named.append(name.data(), name.size());
    }
    return named;
  }
}
