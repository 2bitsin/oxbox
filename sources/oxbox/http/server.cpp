#include "oxbox/http/server.hpp"

#include "oxbox/http/error.hpp"
#include "oxbox/http/response-stream.hpp"

#include <exception>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oxbox::http::detail::server
{
  using namespace std::string_view_literals;

  namespace beast = boost::beast;
  namespace bh    = beast::http;

  using detail::response_stream::ResponseStream;
  using detail::router::Route;
  using detail::router::Router;
  using detail::server_message::BAD_REQUEST;
  using detail::server_message::CONTENT_TOO_LARGE;
  using detail::server_message::INTERNAL_ERROR;
  using detail::server_message::METHOD_NOT_ALLOWED;
  using detail::server_message::NOT_FOUND;
  using detail::server_message::Payload;
  using detail::server_message::ServerRequest;
  using detail::server_message::ServerResponse;
  using detail::server_message::TargetPath;
  using detail::server_message::TargetQuery;

  namespace
  {
    // beast's spelling: 1.1 as major*10 + minor.
    constexpr unsigned HTTP_1_1{ 11u };

    constexpr auto REFUSAL_MEDIA_TYPE{ "text/plain; charset=utf-8"sv };

    constexpr auto NOT_A_HANDLER_THROW{
        "the handler threw something that is not an exception"sv };

    [[noreturn]] auto Refuse(std::string_view what, std::string_view why)
        -> void
    {
      throw TransportError{ std::format("http: {}: {}", what, why) };
    }

    auto Refusal(int status, std::string_view why) -> ServerResponse
    {
      return ServerResponse{
          .status  = status,
          .payload = Payload{ std::string{ REFUSAL_MEDIA_TYPE },
                              std::string{ why } } };
    }

    auto Text(beast::string_view text) noexcept -> std::string_view
    {
      return { text.data(), text.size() };
    }

    auto Bound(asio::any_io_executor const& executor,
               ServerOptions const& options) -> asio::ip::tcp::acceptor
    {
      boost::system::error_code failed{ };
      auto const address{ asio::ip::make_address(options.host, failed) };
      if (failed)
        Refuse(std::format("{} is not an address", options.host),
               failed.message());

      asio::ip::tcp::endpoint const where{ address, options.port };
      asio::ip::tcp::acceptor acceptor{ executor };
      acceptor.open(where.protocol(), failed);
      if (failed)
        Refuse("the listening socket did not open", failed.message());

      acceptor.set_option(asio::socket_base::reuse_address{ true }, failed);
      acceptor.bind(where, failed);
      if (failed)
        Refuse(std::format("{}:{} did not bind", options.host, options.port),
               failed.message());

      acceptor.listen(asio::socket_base::max_listen_connections, failed);
      if (failed)
        Refuse("the socket did not listen", failed.message());
      return acceptor;
    }

    auto RequestFrom(bh::request<bh::string_body>&& message) -> ServerRequest
    {
      ServerRequest request{ };
      request.method = message.method();
      request.target = Text(message.target());
      request.path   = TargetPath(request.target);
      request.query  = TargetQuery(request.target);
      request.content_type = detail::fetch::ParseMediaType(
          Text(message[bh::field::content_type]));
      request.body   = std::move(message.body());
      request.fields = std::move(static_cast<bh::fields&>(message.base()));
      return request;
    }
  }

  struct Server::State {
    ServerOptions           options;
    asio::ip::tcp::acceptor acceptor;
    Router                  router { };
    std::vector<std::weak_ptr<Exchange>> live{ };
    bool                    running{ false };

    auto Remember(std::shared_ptr<Exchange> const& exchange) -> void
    {
      std::erase_if(live, [](auto const& held) { return held.expired(); });
      live.push_back(exchange);
    }
  };

  // One connection, and every request that arrives over it.
  class Server::Exchange: public std::enable_shared_from_this<Exchange> {
  public:
    Exchange(asio::ip::tcp::socket socket, std::shared_ptr<State> state)
    : socket_(std::move(socket))
    , state_(std::move(state))
    { }

    auto Run() -> asio::awaitable<void>;

    // Nothing is aborted: an idle read is cancelled so the loop ends, and an
    // open stream is told, which is what its handler is watching for.
    auto Stop() -> void
    {
      stopping_ = true;
      if (stream_ != nullptr)
        stream_->Close();
      if (reading_) {
        boost::system::error_code ignored{ };
        socket_.cancel(ignored);
      }
    }

  private:
    auto Answer(ServerRequest request, bool keep) -> asio::awaitable<void>;
    auto Streamed(Route const& route, ServerRequest const& request, bool keep)
        -> asio::awaitable<void>;
    auto Deliver(ServerResponse answer, bool keep) -> asio::awaitable<void>;

    asio::ip::tcp::socket  socket_;
    std::shared_ptr<State> state_;
    ResponseStream*        stream_  { nullptr };
    bool                   reading_ { false   };
    bool                   stopping_{ false   };
  };

  auto Server::Exchange::Run() -> asio::awaitable<void>
  {
    beast::flat_buffer pending{ };
    for (;;) {
      bh::request_parser<bh::string_body> parser{ };
      parser.header_limit(state_->options.max_head_bytes);
      parser.body_limit(state_->options.max_body_bytes);

      reading_ = true;
      auto const [failed, ignored] = co_await bh::async_read(
          socket_, pending, parser, asio::as_tuple(asio::deferred));
      reading_ = false;

      if (failed) {
        if (failed == bh::error::body_limit)
          co_await Deliver(Refusal(CONTENT_TOO_LARGE,
                                   "the body is larger than this server takes"),
                           false);
        else if (failed != bh::error::end_of_stream && !stopping_)
          co_await Deliver(Refusal(BAD_REQUEST, failed.message()), false);
        break;
      }

      auto message{ parser.release() };
      bool const keep{ message.keep_alive() && !stopping_ };
      co_await Answer(RequestFrom(std::move(message)), keep);
      if (!keep)
        break;
    }

    boost::system::error_code ignored{ };
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
  }

  auto Server::Exchange::Answer(ServerRequest request, bool keep)
      -> asio::awaitable<void>
  {
    Route const* const route{ state_->router.Match(request.method,
                                                   request.path) };
    if (route == nullptr) {
      if (!state_->router.Knows(request.path)) {
        co_await Deliver(Refusal(NOT_FOUND, "no handler answers this path"),
                         keep);
        co_return;
      }
      ServerResponse answer{ Refusal(METHOD_NOT_ALLOWED,
                                     "this path answers other methods") };
      answer.fields.set(bh::field::allow,
                        state_->router.Allowed(request.path));
      co_await Deliver(std::move(answer), keep);
      co_return;
    }

    if (route->stream) {
      co_await Streamed(*route, request, keep);
      co_return;
    }

    std::string    refusal{ };
    ServerResponse answer { };
    try {
      answer = route->answer(request);
    } catch (std::exception const& thrown) {
      refusal = thrown.what();
    } catch (...) {
      refusal = NOT_A_HANDLER_THROW;
    }
    co_await Deliver(refusal.empty() ? std::move(answer)
                                     : Refusal(INTERNAL_ERROR, refusal),
                     keep);
  }

  auto Server::Exchange::Streamed(Route const& route,
                                  ServerRequest const& request, bool keep)
      -> asio::awaitable<void>
  {
    ResponseStream stream{ socket_, keep };
    stream_ = &stream;
    if (stopping_)
      stream.Close();

    std::string refusal{ };
    try {
      co_await route.stream(request, stream);
    } catch (std::exception const& thrown) {
      refusal = thrown.what();
    } catch (...) {
      refusal = NOT_A_HANDLER_THROW;
    }
    stream_ = nullptr;

    // Once the head is out the status is spent, and all that is left to do
    // with a refusal is stop writing.
    if (!refusal.empty() && !stream.Began()) {
      co_await Deliver(Refusal(INTERNAL_ERROR, refusal), keep);
      co_return;
    }
    co_await stream.End();
  }

  auto Server::Exchange::Deliver(ServerResponse answer, bool keep)
      -> asio::awaitable<void>
  {
    bh::response<bh::string_body> message{ };
    message.version(HTTP_1_1);
    message.result(static_cast<unsigned>(answer.status));
    if (!answer.reason.empty())
      message.reason(answer.reason);
    for (auto const& field : answer.fields)
      message.insert(field.name_string(), field.value());
    if (answer.payload) {
      message.set(bh::field::content_type, answer.payload->content_type);
      message.body() = std::move(answer.payload->body);
    }
    message.keep_alive(keep);
    message.prepare_payload();

    auto const [failed, ignored] = co_await bh::async_write(
        socket_, message, asio::as_tuple(asio::deferred));
    static_cast<void>(failed);
  }

  Server::Server(asio::any_io_executor executor, ServerOptions options)
  : executor_(std::move(executor))
  , state_(std::make_shared<State>(std::move(options),
                                   Bound(executor_, options)))
  { }

  Server::~Server()
  {
    Stop();
  }

  auto Server::On(Method method, std::string path, Handler handler) -> Server&
  {
    state_->router.Add(Route{ method, std::move(path), std::move(handler),
                              nullptr });
    return *this;
  }

  auto Server::OnStream(Method method, std::string path,
                        StreamHandler handler) -> Server&
  {
    state_->router.Add(Route{ method, std::move(path), nullptr,
                              std::move(handler) });
    return *this;
  }

  auto Server::Start() -> void
  {
    if (state_->running)
      return;
    state_->running = true;
    asio::co_spawn(executor_, Accept(state_), asio::detached);
  }

  auto Server::Stop() -> void
  {
    state_->running = false;
    boost::system::error_code ignored{ };
    state_->acceptor.close(ignored);
    for (auto const& held : state_->live)
      if (auto const exchange{ held.lock() })
        exchange->Stop();
    state_->live.clear();
  }

  auto Server::Port() const -> std::uint16_t
  {
    boost::system::error_code failed{ };
    auto const where{ state_->acceptor.local_endpoint(failed) };
    return failed ? 0u : where.port();
  }

  auto Server::Running() const noexcept -> bool
  {
    return state_->running;
  }

  auto Server::Accept(std::shared_ptr<State> state) -> asio::awaitable<void>
  {
    for (;;) {
      auto [failed, socket] = co_await state->acceptor.async_accept(
          asio::as_tuple(asio::deferred));
      if (failed)
        break;

      auto exchange{ std::make_shared<Exchange>(std::move(socket), state) };
      state->Remember(exchange);
      asio::co_spawn(co_await asio::this_coro::executor,
                     Serve(std::move(exchange)), asio::detached);
    }
    state->running = false;
  }

  // The shared_ptr is a parameter and so lives in this frame: a connection
  // outlives the accept turn that made it.
  auto Server::Serve(std::shared_ptr<Exchange> exchange)
      -> asio::awaitable<void>
  {
    co_await exchange->Run();
  }
}
