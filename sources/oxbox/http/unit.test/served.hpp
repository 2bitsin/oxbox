#pragma once
// The loopback server every server suite drives: bound on port 0 in the
// caller's io_context, answered by this module's own client, and stopped by
// whatever the case was spawned with so the loop drains.

#include "oxbox/http/asio.hpp"
#include "oxbox/http/fetch.hpp"
#include "oxbox/http/server.hpp"

#include <chrono>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace oxbox::http::test
{
  namespace asio = boost::asio;

  using namespace std::chrono_literals;
  using namespace std::string_view_literals;

  // long enough that a client read finishes one chunk before the next is
  // written, short enough to cost nothing
  inline constexpr auto BEAT{ 20ms };

  inline constexpr auto SSE_MEDIA_TYPE{ "text/event-stream; charset=utf-8"sv };

  inline auto Where(http::Server const& server, std::string_view path)
      -> std::string
  {
    return std::format("http://127.0.0.1:{}{}", server.Port(), path);
  }

  // The closure is a temporary of the full expression that also runs the
  // loop, which is what keeps a lambda coroutine's captures alive.
  inline auto Ran(asio::io_context& loop, http::Server& server,
                  asio::awaitable<void> work) -> std::exception_ptr
  {
    std::exception_ptr failure{ };
    asio::co_spawn(loop, std::move(work),
                   [&failure, &server](std::exception_ptr thrown) {
                     failure = thrown;
                     server.Stop();
                   });
    loop.run();
    return failure;
  }

  inline auto Rest(std::chrono::milliseconds how_long) -> asio::awaitable<void>
  {
    asio::steady_timer clock{ co_await asio::this_coro::executor };
    clock.expires_after(how_long);
    co_await clock.async_wait(asio::deferred);
  }
}
