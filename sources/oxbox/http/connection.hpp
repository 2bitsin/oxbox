#pragma once
// The byte pipe under an exchange. A TLS certificate is verified against the
// system trust store and against the name asked for, which is sent as SNI.

#include "oxbox/http/asio.hpp"
#include "oxbox/http/url.hpp"

#include <chrono>
#include <cstddef>
#include <span>
#include <variant>

namespace oxbox::http::detail::connection
{
  namespace asio = boost::asio;

  using detail::url::Endpoint;

  using Deadline = std::chrono::steady_clock::time_point;

  using Step = asio::experimental::coro<void, void>;

  // Non-empty means bytes arrived; empty with !Closed() means the heartbeat
  // elapsed on a quiet socket; empty with Closed() means the peer hung up.
  using Reading = asio::experimental::coro<void, std::span<std::byte>>;

  class Connection {
  public:
    Connection(asio::any_io_executor executor, Endpoint endpoint);

    Connection(Connection const&)                          = delete;
    auto operator = (Connection const&) -> Connection&     = delete;
    Connection(Connection&&)                               = delete;
    auto operator = (Connection&&) -> Connection&          = delete;
    ~Connection()                                          = default;

    auto get_executor() -> asio::any_io_executor;

    // Throws TransportError on any of the three steps, naming which.
    auto Open(Deadline deadline) -> Step;

    // The bytes must outlive the step.
    auto Send(std::span<std::byte const> bytes, Deadline deadline) -> Step;

    // The buffer must outlive the step. Throws TransportError on an empty
    // buffer, a read after Closed(), a read failure, or the deadline.
    auto Receive(std::span<std::byte> buffer, Deadline deadline,
                 std::chrono::milliseconds heartbeat) -> Reading;

    [[nodiscard]] auto Closed() const noexcept -> bool;

  private:
    using PlainStream = asio::ip::tcp::socket;
    using TlsStream   = asio::ssl::stream<asio::ip::tcp::socket>;
    using Stream      = std::variant<PlainStream, TlsStream>;

    template <typename _Stream> auto OpenOn(_Stream& stream, Deadline deadline) -> Step;
    template <typename _Stream> auto SendOn(_Stream& stream,
                                            std::span<std::byte const> bytes,
                                            Deadline deadline) -> Step;
    template <typename _Stream> auto ReceiveOn(_Stream& stream,
                                               std::span<std::byte> buffer,
                                               Deadline deadline,
                                               std::chrono::milliseconds heartbeat)
        -> Reading;

    Endpoint endpoint_;
    Stream   stream_;
    bool     closed_{ false };
  };
}

namespace oxbox::http
{
  using detail::connection::Connection;
  using detail::connection::Deadline;
}
