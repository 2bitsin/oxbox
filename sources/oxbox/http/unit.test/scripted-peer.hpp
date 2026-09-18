#pragma once
// The loopback peer every fetch suite drives, answering in pieces with a
// pause between them so a boundary lands where a test wants one.

#include "oxbox/http/asio.hpp"
#include "oxbox/http/fetch.hpp"

#include "oxbox/http/error.hpp"
#include "oxbox/utilities/span.hpp"
#include "oxbox/utilities/unicode.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oxbox::http::test
{
  namespace asio = boost::asio;

  using namespace std::chrono_literals;
  using namespace std::string_literals;
  using namespace std::string_view_literals;

  using http::TransportError;
  using utilities::Encoding;

  // long enough that a read finishes one piece before the next is written
  inline constexpr auto PAUSE{ 20ms };

  // comfortably under PAUSE, so a quiet socket is looked at more than once
  inline constexpr auto HEARTBEAT{ 5ms };

  inline constexpr auto TICK{ 10ms };

  // one read's worth: this client sends the whole request in one write
  inline constexpr std::size_t REQUEST_BYTES{ 4096u };

  // U+00E9: one byte in Latin-1, two in UTF-8, which tells the two apart
  inline constexpr auto E_ACUTE_8{ "\xc3\xa9"sv };
  inline constexpr auto E_ACUTE_1{ "\xe9"sv     };

  // framed by Content-Length, so the body's end is never the connection's
  inline auto Head(std::string_view content_type, std::size_t length) -> std::string
  {
    return std::format("HTTP/1.1 200 OK\r\nContent-Type: {}\r\nContent-Length: "
                       "{}\r\n\r\n", content_type, length);
  }

  // It keeps the request: what the client puts on the wire is half of what
  // these tests check.
  class ScriptedPeer {
  public:
    explicit ScriptedPeer(asio::io_context& loop)
    : acceptor_(loop, asio::ip::tcp::endpoint{ asio::ip::tcp::v4(), 0u })
    { }

    [[nodiscard]] auto Url() const -> std::string
    {
      return std::format("http://127.0.0.1:{}/", acceptor_.local_endpoint().port());
    }

    [[nodiscard]] auto Port() const -> unsigned short
    {
      return acceptor_.local_endpoint().port();
    }

    [[nodiscard]] auto Asked() const noexcept -> std::string_view { return asked_; }

    auto Serve(std::vector<std::string> pieces) -> asio::awaitable<void>
    {
      auto peer{ co_await acceptor_.async_accept(asio::deferred) };

      // Answering earlier would let two pieces land in one read.
      std::array<char, REQUEST_BYTES> asked{ };
      auto const count{ co_await peer.async_read_some(asio::buffer(asked),
                                                      asio::deferred) };
      asked_.assign(asked.data(), count);

      asio::steady_timer clock{ co_await asio::this_coro::executor };
      for (std::string const& piece : pieces) {
        clock.expires_after(PAUSE);
        co_await clock.async_wait(asio::deferred);
        co_await asio::async_write(peer, asio::buffer(piece), asio::deferred);
      }
    }

  private:
    asio::ip::tcp::acceptor acceptor_;
    std::string             asked_{ };
  };

  struct Delivered {
    std::string status_line { };
    std::string body        { };
  };

  // What the door hands over is already the encoding the request asked for.
  inline auto CollectText(http::Request request, Encoding wanted, Delivered& into)
      -> asio::awaitable<void>
  {
    auto reply{ co_await http::Send(std::move(request), wanted) };
    into.status_line = std::format("{} {}", reply.status, reply.reason);
    into.body        = std::move(reply.body);
  }

  inline auto CollectBytes(http::Request request, Delivered& into) -> asio::awaitable<void>
  {
    auto reply{ co_await http::Send(std::move(request), http::UseBytes) };
    into.status_line = std::format("{} {}", reply.status, reply.reason);
    auto const letters{ utilities::SpanCast<char const>(std::span{ reply.body }) };
    into.body.assign(letters.data(), letters.size());
  }

  // the head alone, kept after the reply that carried it is gone
  inline auto CollectHead(http::Request request, std::optional<http::Response>& into)
      -> asio::awaitable<void>
  {
    auto reply{ co_await http::Send(std::move(request), http::UseBytes) };
    into = std::move(static_cast<http::Response&>(reply));
  }

  // A lambda coroutine is spawned through here and never by a co_spawn
  // statement of its own: the captures live in the closure and the frame only
  // points at it, so a closure dying at the semicolon leaves the frame aimed
  // at nothing. Here it is a temporary of the expression that runs the loop.
  inline auto Thrown(asio::io_context& loop, asio::awaitable<void> work) -> std::exception_ptr
  {
    std::exception_ptr failure{ };
    asio::co_spawn(loop, std::move(work),
                   [&failure](std::exception_ptr thrown) { failure = thrown; });
    loop.run();
    return failure;
  }

  inline auto Exchange(std::vector<std::string> pieces, Encoding wanted, Delivered& into)
      -> std::exception_ptr
  {
    asio::io_context loop{ };
    ScriptedPeer peer{ loop };
    http::Request request{ .url = peer.Url(), .heartbeat = HEARTBEAT };

    asio::co_spawn(loop, peer.Serve(std::move(pieces)), asio::detached);
    return Thrown(loop, CollectText(std::move(request), wanted, into));
  }

  inline auto OpaqueExchange(std::vector<std::string> pieces, Delivered& into)
      -> std::exception_ptr
  {
    asio::io_context loop{ };
    ScriptedPeer peer{ loop };
    http::Request request{ .url = peer.Url(), .heartbeat = HEARTBEAT };

    asio::co_spawn(loop, peer.Serve(std::move(pieces)), asio::detached);
    return Thrown(loop, CollectBytes(std::move(request), into));
  }

  // run one request and answer what went out
  inline auto Sent(http::Request shape) -> std::string
  {
    asio::io_context loop{ };
    ScriptedPeer peer{ loop };
    shape.url = std::format("http://127.0.0.1:{}{}", peer.Port(), shape.url);

    Delivered ignored{ };
    asio::co_spawn(loop, peer.Serve({ Head("text/plain", 2u) + "ok" }),
                   asio::detached);
    asio::co_spawn(loop, CollectBytes(std::move(shape), ignored), asio::detached);
    loop.run();
    return std::string{ peer.Asked() };
  }
}
