// One request answered in one piece: the round trip, what a path nobody
// registered answers, what a method nobody registered answers, a handler
// that throws, and the kept-alive connection a second request rides.

#include "oxbox/http/unit.test/served.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <exception>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

using namespace oxbox;
using namespace oxbox::http::test;

namespace
{
  namespace bh = boost::beast::http;

  constexpr auto MCP{ "/mcp" };

  constexpr auto ASKED{ R"({"jsonrpc":"2.0","method":"ping","id":1})" };

  auto Echo(http::ServerRequest const& request) -> http::ServerResponse
  {
    return http::Json(std::format(R"({{"heard":{}}})", request.body));
  }

  auto Echoes(http::Server& server) -> void
  {
    server.On(http::Method::post, MCP, Echo);
  }

  auto Ask(std::string url, std::string body, std::string& into)
      -> asio::awaitable<void>
  {
    auto reply{ co_await http::Post(std::move(url), std::move(body)) };
    into = std::move(reply.body);
  }
}

TEST(HttpServer, BindsPortZeroAndSaysWhichItGot)
{
  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };

  EXPECT_NE(server.Port(), 0u);
  EXPECT_FALSE(server.Running());
  server.Start();
  EXPECT_TRUE(server.Running());
}

TEST(HttpServer, AnswersAJsonPostThroughTheModulesOwnClient)
{
  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };
  Echoes(server);
  server.Start();

  http::TextReply reply{ };
  auto const failure{ Ran(loop, server, [&]() -> asio::awaitable<void> {
    reply = co_await http::Post(Where(server, MCP), ASKED);
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(reply.status, http::OK);
  EXPECT_EQ(reply.content_type.Type(), http::JSON_MEDIA_TYPE);
  EXPECT_EQ(reply.body, std::format(R"({{"heard":{}}})", ASKED));
}

TEST(HttpServer, APathNobodyRegisteredIsNotFound)
{
  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };
  Echoes(server);
  server.Start();

  http::TextReply reply{ };
  auto const failure{ Ran(loop, server, [&]() -> asio::awaitable<void> {
    reply = co_await http::Fetch(Where(server, "/nowhere"));
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(reply.status, http::NOT_FOUND);
}

TEST(HttpServer, AKnownPathUnderAnotherMethodSaysWhichItTakes)
{
  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };
  Echoes(server);
  server.OnStream(http::Method::get, MCP,
                  [](http::ServerRequest const&,
                     http::ResponseStream& stream) -> asio::awaitable<void> {
                    co_await stream.Begin(http::OK);
                  });
  server.Start();

  http::TextReply reply{ };
  auto const failure{ Ran(loop, server, [&]() -> asio::awaitable<void> {
    reply = co_await http::Delete(Where(server, MCP));
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(reply.status, http::METHOD_NOT_ALLOWED);
  ASSERT_TRUE(reply["Allow"].has_value());
  EXPECT_EQ(*reply["Allow"], "POST, GET");
}

TEST(HttpServer, AHandlerThatThrowsAnswersFiveHundredWithWhatItSaid)
{
  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };
  server.On(http::Method::post, MCP,
            [](http::ServerRequest const&) -> http::ServerResponse {
              throw std::runtime_error{ "the session id is not one of mine" };
            });
  server.Start();

  http::TextReply reply{ };
  auto const failure{ Ran(loop, server, [&]() -> asio::awaitable<void> {
    reply = co_await http::Post(Where(server, MCP), ASKED);
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(reply.status, http::INTERNAL_ERROR);
  EXPECT_EQ(reply.body, "the session id is not one of mine");
}

// This module's client sends Connection: close on every request, so the
// reuse it does not do is asked for here by hand.
TEST(HttpServer, TwoRequestsRideOneKeptAliveConnection)
{
  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };
  Echoes(server);
  server.Start();

  int  answered{ 0 };
  bool reused  { false };
  auto const failure{ Ran(loop, server, [&]() -> asio::awaitable<void> {
    asio::ip::tcp::socket socket{ co_await asio::this_coro::executor };
    co_await socket.async_connect(
        asio::ip::tcp::endpoint{ asio::ip::make_address("127.0.0.1"),
                                 server.Port() },
        asio::deferred);

    auto const asking{ std::format(
        "POST {} HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: "
        "application/json\r\nContent-Length: {}\r\n\r\n{}",
        MCP, std::string_view{ ASKED }.size(), ASKED) };

    boost::beast::flat_buffer pending{ };
    for (int turn{ 0 }; turn < 2; ++turn) {
      co_await asio::async_write(socket, asio::buffer(asking),
                                 asio::deferred);
      bh::response<bh::string_body> answer{ };
      co_await bh::async_read(socket, pending, answer, asio::deferred);
      answered += answer.result_int() == http::OK ? 1 : 0;
      reused = answer.keep_alive();
    }
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(answered, 2);
  EXPECT_TRUE(reused);
}

TEST(HttpServer, EveryClientOfABurstIsAnsweredItsOwnAnswer)
{
  constexpr int CLIENTS{ 16 };

  asio::io_context loop{ };
  http::Server server{ loop.get_executor(), http::ServerOptions{ } };
  Echoes(server);
  server.Start();

  int                      answered{ 0 };
  std::vector<std::string> heard(CLIENTS);
  for (int client{ 0 }; client < CLIENTS; ++client)
    asio::co_spawn(loop,
                   Ask(Where(server, MCP), std::format("{}", client),
                       heard[static_cast<std::size_t>(client)]),
                   [&answered, &server](std::exception_ptr thrown) {
                     EXPECT_FALSE(thrown);
                     if (++answered == CLIENTS)
                       server.Stop();
                   });
  loop.run();

  EXPECT_EQ(answered, CLIENTS);
  for (int client{ 0 }; client < CLIENTS; ++client)
    EXPECT_EQ(heard[static_cast<std::size_t>(client)],
              std::format(R"({{"heard":{}}})", client));
}
