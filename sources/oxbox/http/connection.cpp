#include "oxbox/http/asio.hpp"
#include "oxbox/http/connection.hpp"

#include "oxbox/http/error.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

namespace oxbox::http::detail::connection
{
  namespace
  {
    // The names are verbs: a message reads "could not connect to X".
    enum class Stage: std::uint8_t { RESOLVE, CONNECT, HANDSHAKE };

    auto StageName(Stage stage) noexcept -> std::string_view
    {
      switch (stage) {
        case Stage::RESOLVE:   return "resolve";
        case Stage::CONNECT:   return "connect to";
        case Stage::HANDSHAKE: return "negotiate TLS with";
      }
      std::unreachable();  // Stage is closed and every enumerator is above
    }

    // Bytes a TLS session is already holding, decrypted or not: waiting for
    // the socket to become readable would stall on exactly those, because
    // they have already arrived. A plain connection holds nothing of its own.
    template <typename _Stream>
    auto Buffered(_Stream& stream) noexcept -> bool
    {
      if constexpr (std::same_as<_Stream, asio::ssl::stream<asio::ip::tcp::socket>>)
        return SSL_has_pending(stream.native_handle()) != 0;
      else
        return false;
    }

    // One context for the process: building it loads the system trust store.
    auto TlsContext() -> asio::ssl::context&
    {
      // OpenSSL refcounts what a session takes from it, so this outliving
      // every stream built on it is safe
      static asio::ssl::context context{ [ ] {
        asio::ssl::context fresh{ asio::ssl::context::tls_client };
        // the system CA bundle, so a certificate curl accepts is accepted here
        fresh.set_default_verify_paths();
        fresh.set_verify_mode(asio::ssl::verify_peer);
        return fresh;
      }() };
      return context;
    }
  }

  Connection::Connection(asio::any_io_executor executor, Endpoint endpoint)
  : endpoint_(std::move(endpoint))
  , stream_(endpoint_.scheme == url::Scheme::HTTPS
                ? Stream{ std::in_place_type<TlsStream>, executor, TlsContext() }
                : Stream{ std::in_place_type<PlainStream>, executor })
  { }

  auto Connection::get_executor() -> asio::any_io_executor
  {
    return std::visit([](auto& stream) -> asio::any_io_executor {
      return stream.lowest_layer().get_executor();
    }, stream_);
  }

  auto Connection::Open(Deadline deadline) -> Step
  {
    co_await std::visit([&](auto& stream) { return OpenOn(stream, deadline); }, stream_);
  }

  auto Connection::Send(std::span<std::byte const> bytes, Deadline deadline) -> Step
  {
    co_await std::visit([&](auto& stream) { return SendOn(stream, bytes, deadline); },
                        stream_);
  }

  auto Connection::Closed() const noexcept -> bool
  {
    return closed_;
  }

  auto Connection::Receive(std::span<std::byte> buffer, Deadline deadline,
                           std::chrono::milliseconds heartbeat) -> Reading
  {
    // Refused before any I/O: either would forge a silence nobody waited for.
    if (buffer.empty())
      throw TransportError{ std::format(
          "http connection: a read from {}:{} was given no buffer to fill",
          endpoint_.host, endpoint_.port) };
    if (closed_)
      throw TransportError{ std::format(
          "http connection: {}:{} has already closed the connection — nothing "
          "will arrive on it again", endpoint_.host, endpoint_.port) };

    co_return co_await std::visit(
        [&](auto& stream) { return ReceiveOn(stream, buffer, deadline, heartbeat); },
        stream_);
  }

  template <typename _Stream>
  auto Connection::OpenOn(_Stream& stream, Deadline deadline) -> Step
  {
    auto stage{ Stage::RESOLVE };
    try {
      asio::ip::tcp::resolver resolver{ stream.lowest_layer().get_executor() };
      auto const where{ co_await resolver.async_resolve(
          endpoint_.host, endpoint_.port, asio::cancel_at(deadline)) };

      stage = Stage::CONNECT;
      co_await asio::async_connect(stream.lowest_layer(), where,
                                   asio::cancel_at(deadline));
      // Nagle would hold a small request back waiting for more; the whole
      // request goes out in one write.
      stream.lowest_layer().set_option(asio::ip::tcp::no_delay{ true });

      if constexpr (std::same_as<_Stream, TlsStream>) {
        stage = Stage::HANDSHAKE;
        // Against the name asked for and not merely against the trust store:
        // a valid certificate belonging to somebody else is what this refuses.
        stream.set_verify_callback(
            asio::ssl::host_name_verification{ endpoint_.host });
        // SNI: a virtually hosted server answers a handshake without the
        // extension with somebody else's certificate, which then fails the
        // check above. OpenSSL exposes it as a macro; asio wraps none of it.
        if (SSL_set_tlsext_host_name(stream.native_handle(),
                                     endpoint_.host.c_str()) != 1)
          throw TransportError{ std::format(
              "http connection: cannot set the TLS server name to '{}'",
              endpoint_.host) };
        co_await stream.async_handshake(asio::ssl::stream_base::client,
                                        asio::cancel_at(deadline));
      }

    } catch (boost::system::system_error const& failure) {
      // A deadline reached mid-step arrives as a cancellation, and
      // "Operation canceled" would name nothing that timed out.
      auto const because{ failure.code() == asio::error::operation_aborted
                              ? std::string{ "timed out" }
                              : failure.code().message() };
      throw TransportError{ std::format("http connection: could not {} {}:{} — {}",
                                        StageName(stage), endpoint_.host,
                                        endpoint_.port, because) };
    }
  }

  template <typename _Stream>
  auto Connection::SendOn(_Stream& stream, std::span<std::byte const> bytes,
                          Deadline deadline) -> Step
  {
    try {
      co_await asio::async_write(stream, asio::buffer(bytes.data(), bytes.size()),
                                 asio::cancel_at(deadline));
    } catch (boost::system::system_error const& failure) {
      throw TransportError{ std::format(
          "http connection: could not send the request to {}:{} — {}",
          endpoint_.host, endpoint_.port, failure.code().message()) };
    }
  }

  template <typename _Stream>
  auto Connection::ReceiveOn(_Stream& stream, std::span<std::byte> buffer,
                             Deadline deadline, std::chrono::milliseconds heartbeat)
      -> Reading
  {
    // The read itself is never cancelled: asio filters TLS cancellation down
    // to `terminal` (ssl/detail/io.hpp), so a `partial` heartbeat on a TLS
    // read is dropped and `terminal` wrecks the session. The heartbeat waits
    // for readability instead -- a wait consumes nothing.
    if (!Buffered(stream)) {
      auto const [waited] = co_await stream.lowest_layer().async_wait(
          asio::ip::tcp::socket::wait_read,
          asio::as_tuple(
              asio::cancel_after(heartbeat, asio::cancellation_type::partial)));

      if (waited == asio::error::operation_aborted) {
        if (std::chrono::steady_clock::now() >= deadline)
          throw TransportError{ std::format(
              "http connection: {}:{} gave no answer before the deadline",
              endpoint_.host, endpoint_.port) };
        co_return std::span<std::byte>{ };
      }
      if (waited)
        throw TransportError{ std::format(
            "http connection: could not wait on {}:{} — {}", endpoint_.host,
            endpoint_.port, waited.message()) };
    }

    // Readable, so this returns promptly -- except for a TLS record that
    // arrived in pieces, which costs a round trip. A read's failure is often
    // not one -- a peer closing ends a read-to-close body -- so the error
    // code is a value to branch on here.
    auto const [failure, count] =
        co_await stream.async_read_some(asio::buffer(buffer.data(), buffer.size()),
                                        asio::as_tuple(asio::deferred));

    // A TLS peer that closed without close_notify is truncated rather than
    // eof; both mean the same thing to a reader.
    if (failure == asio::error::eof ||
        failure == asio::ssl::error::stream_truncated) {
      closed_ = true;
      co_return std::span<std::byte>{ };
    }
    if (failure)
      throw TransportError{ std::format(
          "http connection: could not read the response from {}:{} — {}",
          endpoint_.host, endpoint_.port, failure.message()) };

    co_return buffer.first(count);
  }
}
