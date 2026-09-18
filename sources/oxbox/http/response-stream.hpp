#pragma once
// A response written over time, framed by chunked transfer coding (RFC 9112
// 7.1): the head once, then chunks, then the end. What an event stream needs.

#include "oxbox/http/asio.hpp"
#include "oxbox/http/server-message.hpp"

#include <optional>
#include <string_view>

namespace oxbox::http::detail::response_stream
{
  namespace asio  = boost::asio;
  namespace beast = boost::beast;

  using detail::server_message::FieldTable;
  using detail::server_message::OK;

  class ResponseStream {
  public:
    ResponseStream(asio::ip::tcp::socket& socket, bool keep_alive) noexcept;

    ResponseStream(ResponseStream const&)                      = delete;
    auto operator = (ResponseStream const&) -> ResponseStream& = delete;
    ResponseStream(ResponseStream&&)                           = delete;
    auto operator = (ResponseStream&&) -> ResponseStream&      = delete;
    ~ResponseStream()                                          = default;

    // The head, once and before any chunk. A second call does nothing.
    auto Begin(int status, FieldTable fields) -> asio::awaitable<void>;
    auto Begin(int status) -> asio::awaitable<void>;

    // Begins with 200 when the head has not gone. The chunk must outlive
    // the await.
    auto Write(std::string_view chunk) -> asio::awaitable<void>;

    auto End() -> asio::awaitable<void>;

    // False once the stream ended, the socket failed, or Stop() ran: a
    // handler's loop asks this and winds up.
    [[nodiscard]] auto Open() const noexcept -> bool
    { return began_ && !ended_ && !stopped_ && !failed_; }

    [[nodiscard]] auto Began() const noexcept -> bool { return began_; }

    auto Close() noexcept -> void { stopped_ = true; }

  private:
    using Head = beast::http::response<beast::http::empty_body>;

    asio::ip::tcp::socket* socket_;
    Head                   head_  { };
    std::optional<beast::http::response_serializer<beast::http::empty_body>>
                           serial_{ };
    bool                   keep_  { false };
    bool                   began_  { false };
    bool                   ended_  { false };
    bool                   stopped_{ false };
    bool                   failed_ { false };
  };
}

namespace oxbox::http
{
  using detail::response_stream::ResponseStream;
}
