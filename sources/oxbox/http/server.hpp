#pragma once
// An HTTP/1.1 server on the caller's executor: it binds while it is built,
// so the port is known before anything is accepted, and every handler runs
// on that executor. Plain TCP, behind whatever terminates TLS.

#include "oxbox/http/asio.hpp"
#include "oxbox/http/router.hpp"
#include "oxbox/http/server-message.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace oxbox::http::detail::server
{
  namespace asio = boost::asio;

  using detail::router::Handler;
  using detail::router::StreamHandler;
  using detail::server_message::Method;

  struct ServerOptions {
    std::string   host           { "127.0.0.1"  };
    // 0 asks the host for a free port, which Port() then answers with.
    std::uint16_t port           { 0            };
    std::size_t   max_head_bytes { 64u * 1024u  };
    std::size_t   max_body_bytes { 1024u * 1024u };
  };

  class Server {
  public:
    // Throws TransportError naming the address when the bind fails.
    Server(asio::any_io_executor executor, ServerOptions options);

    Server(Server const&)                      = delete;
    auto operator = (Server const&) -> Server& = delete;
    Server(Server&&)                           = delete;
    auto operator = (Server&&) -> Server&      = delete;
    ~Server();

    auto On(Method method, std::string path, Handler handler) -> Server&;
    auto OnStream(Method method, std::string path, StreamHandler handler)
        -> Server&;

    // Spawns the accept loop on the executor; nothing is served before it.
    auto Start() -> void;

    // Stops accepting and asks every open stream to end. A response already
    // being written finishes. Call it from the thread running the executor.
    auto Stop() -> void;

    [[nodiscard]] auto Port() const -> std::uint16_t;
    [[nodiscard]] auto Running() const noexcept -> bool;

  private:
    struct State;
    class Exchange;

    static auto Accept(std::shared_ptr<State> state) -> asio::awaitable<void>;
    static auto Serve(std::shared_ptr<Exchange> exchange)
        -> asio::awaitable<void>;

    asio::any_io_executor  executor_;
    std::shared_ptr<State> state_;
  };
}

namespace oxbox::http
{
  using detail::server::Server;
  using detail::server::ServerOptions;
}
