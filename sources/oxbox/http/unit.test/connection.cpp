#include "oxbox/http/asio.hpp"
#include "oxbox/http/connection.hpp"

#include "oxbox/http/error.hpp"
#include "oxbox/http/url.hpp"
#include "oxbox/utilities/span.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <span>
#include <string>
#include <string_view>

// The one test here that needs a socket, because the thing under test is the
// socket: every other part of this module is byte work and is tested
// with fixtures (see fetch.hpp). The peer is a loopback acceptor in this
// same io_context — no network, no fixture server, no port anybody else
// could be holding.

using namespace oxbox;

namespace
{
  namespace asio = boost::asio;

  using namespace std::chrono_literals;
  using namespace std::string_view_literals;

  using http::Connection;
  using http::Deadline;
  using http::TransportError;
  using http::SplitUrl;

  constexpr auto HEARTBEAT{ 20ms };

  auto Soon() -> Deadline
  {
    return std::chrono::steady_clock::now() + 5s;
  }

  // A peer that says its piece and hangs up — the shape of an HTTP/1.1
  // response with `Connection: close`, which is the only shape this
  // client ever talks to.
  class OneShotPeer {
  public:
    explicit OneShotPeer(asio::io_context& loop)
    : acceptor_(loop, asio::ip::tcp::endpoint{ asio::ip::tcp::v4(), 0u })
    { }

    [[nodiscard]] auto Url() const -> std::string
    {
      return std::format("http://127.0.0.1:{}/", acceptor_.local_endpoint().port());
    }

    // Accepts one connection, writes `answer`, closes. The socket lives
    // in the coroutine's frame, so the close is the frame ending.
    auto Serve(std::string answer) -> asio::awaitable<void>
    {
      auto peer{ co_await acceptor_.async_accept(asio::deferred) };
      co_await asio::async_write(peer, asio::buffer(answer), asio::deferred);
    }

  private:
    asio::ip::tcp::acceptor acceptor_;
  };

  // Run one coroutine to completion on `loop` and hand back whatever it
  // threw, so a test can assert on the failure rather than on a crash.
  auto Thrown(asio::io_context& loop, asio::awaitable<void> work) -> std::exception_ptr
  {
    std::exception_ptr failure{ };
    asio::co_spawn(loop, std::move(work),
                   [&failure](std::exception_ptr thrown) { failure = thrown; });
    loop.run();
    return failure;
  }
}

TEST(Connection, AnEmptyReadBufferIsRefusedRatherThanForgingAnAnswer)
{
  // a zero-byte read completes instantly with zero bytes, which is the
  // empty span that means silence — a sentinel nobody may forge
  asio::io_context loop{ };
  Connection connection{ loop.get_executor(), SplitUrl("http://127.0.0.1:9/") };

  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto reading{ connection.Receive({ }, Soon(), HEARTBEAT) };
    co_await reading.async_resume(asio::deferred);
  }()) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), TransportError);
}

TEST(Connection, BytesThenTheCloseArriveAsASpanThenAnEmptyOne)
{
  asio::io_context loop{ };
  OneShotPeer peer{ loop };
  Connection connection{ loop.get_executor(), SplitUrl(peer.Url()) };

  std::string first{ };
  auto second{ std::span<std::byte>{ } };

  asio::co_spawn(loop, peer.Serve("hello"), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    std::array<std::byte, 64u> buffer{ };
    auto opening{ connection.Open(Soon()) };
    co_await opening.async_resume(asio::deferred);

    // the bytes: a filled prefix of the caller's own buffer
    auto arrival{ connection.Receive(buffer, Soon(), HEARTBEAT) };
    auto const arrived{ co_await arrival.async_resume(asio::deferred) };
    auto const letters{ utilities::SpanCast<char const>(arrived) };
    first.assign(letters.data(), letters.size());
    EXPECT_FALSE(connection.Closed());

    // and then the ending: nothing filled, and the connection says why
    auto ending{ connection.Receive(buffer, Soon(), HEARTBEAT) };
    second = co_await ending.async_resume(asio::deferred);
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(first, "hello");
  EXPECT_TRUE(second.empty());
  EXPECT_TRUE(connection.Closed());
}

TEST(Connection, ReceivingAfterTheCloseWasSeenIsRefused)
{
  // a closed socket is permanently readable: a caller that dropped the
  // Closed() check would spin at full speed calling every instant empty
  // read a heartbeat, and this is what makes that loud instead
  asio::io_context loop{ };
  OneShotPeer peer{ loop };
  Connection connection{ loop.get_executor(), SplitUrl(peer.Url()) };

  asio::co_spawn(loop, peer.Serve("hello"), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    std::array<std::byte, 64u> buffer{ };
    auto opening{ connection.Open(Soon()) };
    co_await opening.async_resume(asio::deferred);

    while (!connection.Closed()) {
      auto reading{ connection.Receive(buffer, Soon(), HEARTBEAT) };
      co_await reading.async_resume(asio::deferred);
    }

    auto again{ connection.Receive(buffer, Soon(), HEARTBEAT) };
    co_await again.async_resume(asio::deferred);
  }()) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), TransportError);
}
