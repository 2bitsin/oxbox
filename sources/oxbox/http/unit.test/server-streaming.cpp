// A response written over time: the chunks a client reads as they arrive,
// and the stop that ends one still open without tearing it off mid-body.

#include "oxbox/http/unit.test/served.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <format>
#include <string>
#include <vector>

using namespace oxbox;
using namespace oxbox::http::test;

namespace
{
  constexpr auto EVENTS{ "/events" };

  // -1 keeps going until the stream says it is over, which is what a
  // server-sent event stream does.
  auto Ticker(int events) -> http::StreamHandler
  {
    return [events](http::ServerRequest const&,
                    http::ResponseStream& stream) -> asio::awaitable<void> {
      http::FieldTable head{ };
      head.set("Content-Type", SSE_MEDIA_TYPE);
      co_await stream.Begin(http::OK, head);

      for (int n{ 0 }; (events < 0 || n < events) && stream.Open(); ++n) {
        co_await Rest(BEAT);
        std::string const event{ std::format("data: {}\n\n", n) };
        co_await stream.Write(event);
      }
    };
  }
}

TEST(HttpServerStream, ChunksReachTheClientOneAtATime)
{
  constexpr int EVENTS_SENT{ 3 };

  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };
  server.OnStream(http::Method::get, EVENTS, Ticker(EVENTS_SENT));
  server.Start();

  std::vector<std::string> seen{ };
  int                      status{ 0 };
  std::string              type  { };
  auto const failure{ Ran(loop, server, [&]() -> asio::awaitable<void> {
    auto stream{ http::Fetch(Where(server, EVENTS), http::UseStream) };
    while (!stream.Done())
      if (auto const chunk{ co_await stream.Next() })
        seen.emplace_back(*chunk);
    status = stream.status;
    type   = stream.content_type.Type();
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(status, http::OK);
  EXPECT_EQ(type, "text/event-stream");
  EXPECT_EQ(seen, (std::vector<std::string>{ "data: 0\n\n", "data: 1\n\n",
                                             "data: 2\n\n" }));
}

TEST(HttpServerStream, StopEndsAnOpenStreamInsteadOfCuttingIt)
{
  constexpr std::size_t ENOUGH{ 2u };

  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };
  server.OnStream(http::Method::get, EVENTS, Ticker(-1));
  server.Start();

  std::size_t seen{ 0u };
  bool        ended{ false };
  auto const failure{ Ran(loop, server, [&]() -> asio::awaitable<void> {
    auto stream{ http::Fetch(Where(server, EVENTS), http::UseStream) };
    while (!stream.Done()) {
      if (auto const chunk{ co_await stream.Next() })
        ++seen;
      if (seen == ENOUGH && server.Running())
        server.Stop();
    }
    ended = stream.Done();
  }()) };

  // A cut stream is a TransportError: the body announced by the framing
  // never finished. This one finished, which is what graceful means here.
  ASSERT_FALSE(failure);
  EXPECT_TRUE(ended);
  EXPECT_GE(seen, ENOUGH);
  EXPECT_FALSE(server.Running());
}
