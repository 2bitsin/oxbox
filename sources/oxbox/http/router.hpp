#pragma once
// Which handler answers a request: method and path, matched exactly. A path
// nobody registered is 404; one registered under other methods only is 405,
// and Allow says which they are.

#include "oxbox/http/asio.hpp"
#include "oxbox/http/response-stream.hpp"
#include "oxbox/http/server-message.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oxbox::http::detail::router
{
  namespace asio = boost::asio;

  using detail::response_stream::ResponseStream;
  using detail::server_message::Method;
  using detail::server_message::ServerRequest;
  using detail::server_message::ServerResponse;

  // Throwing is how it refuses: the message becomes the 500 that goes out.
  using Handler = std::function<ServerResponse(ServerRequest const&)>;

  using StreamHandler = std::function<
      asio::awaitable<void>(ServerRequest const&, ResponseStream&)>;

  struct Route {
    Method        method {  };
    std::string   path   {  };
    Handler       answer {  };
    StreamHandler stream {  };
  };

  class Router {
  public:
    auto Add(Route route) -> void;

    // Nullptr for an unknown path and the wrong method alike; Knows()
    // separates them.
    [[nodiscard]] auto Match(Method method, std::string_view path) const
        -> Route const*;

    [[nodiscard]] auto Knows(std::string_view path) const -> bool;

    // For Allow, comma-separated in registration order (RFC 9110 10.2.1).
    [[nodiscard]] auto Allowed(std::string_view path) const -> std::string;

  private:
    std::vector<Route> routes_{ };
  };
}

namespace oxbox::http
{
  using detail::router::Handler;
  using detail::router::Route;
  using detail::router::Router;
  using detail::router::StreamHandler;
}
