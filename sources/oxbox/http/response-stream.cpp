#include "oxbox/http/response-stream.hpp"

#include <utility>

namespace oxbox::http::detail::response_stream
{
  namespace bh = beast::http;

  namespace
  {
    // beast's spelling: 1.1 as major*10 + minor.
    constexpr unsigned HTTP_1_1{ 11u };
  }

  ResponseStream::ResponseStream(asio::ip::tcp::socket& socket,
                                 bool keep_alive) noexcept
  : socket_(&socket)
  , keep_(keep_alive)
  { }

  auto ResponseStream::Begin(int status) -> asio::awaitable<void>
  {
    co_await Begin(status, FieldTable{ });
  }

  auto ResponseStream::Begin(int status, FieldTable fields)
      -> asio::awaitable<void>
  {
    if (began_ || stopped_)
      co_return;

    head_.version(HTTP_1_1);
    head_.result(static_cast<unsigned>(status));
    for (auto const& field : fields)
      head_.insert(field.name_string(), field.value());
    head_.chunked(true);
    head_.keep_alive(keep_);

    began_ = true;
    serial_.emplace(head_);
    auto const [failed, ignored] = co_await bh::async_write_header(
        *socket_, *serial_, asio::as_tuple(asio::deferred));
    if (failed)
      failed_ = true;
  }

  auto ResponseStream::Write(std::string_view chunk) -> asio::awaitable<void>
  {
    if (!began_)
      co_await Begin(OK);
    if (!Open() || chunk.empty())
      co_return;

    auto const [failed, ignored] = co_await asio::async_write(
        *socket_, bh::make_chunk(asio::buffer(chunk)),
        asio::as_tuple(asio::deferred));
    if (failed)
      failed_ = true;
  }

  auto ResponseStream::End() -> asio::awaitable<void>
  {
    if (!began_ || ended_)
      co_return;

    // A stop still writes the last chunk: the peer reading this stream is
    // owed its end, and that is what makes Stop graceful.
    ended_ = true;
    if (failed_)
      co_return;
    auto const [failed, ignored] = co_await asio::async_write(
        *socket_, bh::make_chunk_last(), asio::as_tuple(asio::deferred));
    if (failed)
      failed_ = true;
  }
}
