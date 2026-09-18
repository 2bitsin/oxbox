#pragma once
// A minimal HTTP/1.1 client over boost::asio: RFC 9110 (semantics), RFC 9112
// (syntax), one request per connection, no redirects, cookies or compression.
// A body is UTF-8 text and whole unless a trailing tag says otherwise.

#include "oxbox/http/asio.hpp"
#include "oxbox/http/connection.hpp"
#include "oxbox/http/delivery.hpp"
#include "oxbox/http/error.hpp"
#include "oxbox/http/url.hpp"
#include "oxbox/utilities/unicode.hpp"

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace oxbox::http::detail::fetch
{
  namespace asio  = boost::asio;
  namespace beast = boost::beast;

  using namespace std::string_view_literals;

  using utilities::Encoding;

  using FieldTable = beast::http::fields;

  using Method = beast::http::verb;

  // A body and the media type declaring it (RFC 9110 8.3).
  struct Payload {
    // Required: this layer cannot know the type and a guess would be wrong.
    std::string content_type { };
    std::string body         { };
  };

  struct Header {
    std::string name  { };
    std::string value { };  // sent verbatim
  };

  struct Request {
    Method                     method    { Method::get };
    // An absolute http(s) url. Required; anything else is refused.
    std::string                url       {             };
    // Nullopt sends no body and no Content-* field at all.
    std::optional<Payload>     payload   {             };
    // Verbatim and in order. A name repeated here goes out twice (RFC 9110
    // 5.2); one that collides with a header this client derives replaces it,
    // except Content-Length, Transfer-Encoding and Connection.
    std::vector<Header>        headers   {             };
    // The whole transfer's budget, not a per-read idle timer.
    std::chrono::seconds       timeout   { 60          };
    // Socket poll interval while nothing arrives; it bounds the resolution
    // of Stream::Next(interval) and nothing else.
    std::chrono::milliseconds  heartbeat { 100         };
  };

  // Type and parameter names are folded (RFC 9110 8.3.1); a parameter value
  // is kept as written.
  class MediaType {
  public:
    // Empty when the response carried no Content-Type.
    [[nodiscard]] auto Type() const noexcept -> std::string_view
    { return type_; }

    [[nodiscard]] auto Parameter(std::string_view name) const
        -> std::optional<std::string_view>;

  private:
    friend auto ParseMediaType(std::string_view value) -> MediaType;

    std::string                                  type_       { };
    std::unordered_map<std::string, std::string> parameters_ { };
  };

  // Lenient: a parameter nobody can parse is dropped rather than thrown over.
  auto ParseMediaType(std::string_view value) -> MediaType;

  // The parameter a media type states its encoding in (RFC 9110 §8.3.2).
  constexpr auto CHARSET{ "charset"sv };

  struct Response {
    // 100..599 (RFC 9110 15). A number and not beast's enum: an unregistered
    // code is legal, and the enum has only `unknown` for it.
    int         status       { };
    // Diagnostic only, and synthesised from the code when none was sent.
    std::string reason       { };
    FieldTable  fields       { };
    MediaType   content_type { };

    // Case-insensitive (RFC 9110 5.1); nullopt is absent, not present-and-
    // empty. A repeated field answers the first. The view borrows this object.
    [[nodiscard]] auto operator [] (std::string_view name) const
        -> std::optional<std::string_view>
    {
      auto const found{ fields.find(
          beast::string_view{ name.data(), name.size() }) };
      if (found == fields.end())
        return std::nullopt;
      auto const value{ found->value() };
      return std::string_view{ value.data(), value.size() };
    }
  };

  struct UseStreamTag { };
  inline constexpr UseStreamTag UseStream{ };

  struct UseBytesTag { };
  inline constexpr UseBytesTag UseBytes{ };

  inline constexpr auto TEXT_ENCODING{ Encoding::UTF8 };

  using ByteSteps =
      asio::experimental::coro<std::variant<Response, std::span<std::byte const>>, void>;
  using TextSteps =
      asio::experimental::coro<std::variant<Response, std::string_view>, void>;

  auto StartBytes(asio::any_io_executor executor, Request request) -> ByteSteps;
  auto StartText (asio::any_io_executor executor, Request request,
                  Encoding required) -> TextSteps;

  template <typename _Body>
  struct Reply: Response {
    _Body body { };
  };

  using TextReply  = Reply<std::string>;
  using BytesReply = Reply<std::vector<std::byte>>;

  template <typename _Chunk> class Stream;
  using TextStream  = Stream<std::string_view>;
  using BytesStream = Stream<std::span<std::byte const>>;

  auto Send(Request request) -> asio::awaitable<TextReply>;
  auto Send(Request request, Encoding required) -> asio::awaitable<TextReply>;
  auto Send(Request request, UseBytesTag) -> asio::awaitable<BytesReply>;
  auto Send(Request request, UseStreamTag) -> TextStream;
  auto Send(Request request, Encoding required, UseStreamTag) -> TextStream;
  auto Send(Request request, UseBytesTag, UseStreamTag) -> BytesStream;

  // A Response filled once the head lands; before that the members are
  // defaults and status is 0. Nothing happens until the first Next: that
  // await harvests the caller's executor, connects, sends and reads the head.
  template <typename _Chunk>
  class Stream: public Response {
  public:
    Stream(Stream const&)                        = delete;
    auto operator = (Stream const&) -> Stream&   = delete;
    Stream(Stream&&)                             = default;
    auto operator = (Stream&&) -> Stream&        = default;
    ~Stream()                                    = default;

    [[nodiscard]] auto Answered() const noexcept -> bool
    { return phase_ != Phase::UNSENT; }

    [[nodiscard]] auto Done() const noexcept -> bool
    { return phase_ == Phase::DONE; }

    // With no interval stated a nullopt can only be the end: every chunk has bytes.
    [[nodiscard]] auto Next() -> asio::awaitable<std::optional<_Chunk>>
    {
      while (!Done())
        if (auto const chunk = co_await Arrival(); chunk && !chunk->empty())
          co_return *chunk;
      co_return std::nullopt;
    }

    // Nullopt has two causes here and Done() is what separates them.
    [[nodiscard]] auto Next(std::chrono::milliseconds tick)
        -> asio::awaitable<std::optional<_Chunk>>
    {
      // a floor, not a period: no tick sooner than the request's heartbeat, and
      // it covers the head-wait but not connect or TLS, which read no bytes
      auto const until{ std::chrono::steady_clock::now() + tick };
      while (!Done()) {
        if (auto const chunk = co_await Arrival(); chunk && !chunk->empty())
          co_return *chunk;
        if (std::chrono::steady_clock::now() >= until)
          co_return std::nullopt;                      // nothing yet
      }
      co_return std::nullopt;                          // over
    }

  private:
    // Ordered; nothing moves backwards.
    enum class Phase: std::uint8_t {
      UNSENT,
      ANSWERED,
      DONE
    };

    Stream(Request request, std::optional<Encoding> required)
    : request_(std::move(request))
    , required_(required)
    { }

    [[nodiscard]] auto Arrival() -> asio::awaitable<std::optional<_Chunk>>
    {
      // Never resume a finished coro -- asio answers that with broken_pipe.
      if (Done())
        co_return std::nullopt;

      // the first moment there is an executor to ask for
      if (!steps_) {
        auto executor{ co_await asio::this_coro::executor };
        if constexpr (std::same_as<_Chunk, std::string_view>)
          steps_.emplace(StartText(executor, std::move(request_), *required_));
        else
          steps_.emplace(StartBytes(executor, std::move(request_)));
      }

      auto turn{ co_await steps_->async_resume(asio::deferred) };
      if (!turn) {
        phase_ = Phase::DONE;
        co_return std::nullopt;
      }
      if (auto* const head = std::get_if<Response>(&*turn)) {
        static_cast<Response&>(*this) = std::move(*head);
        phase_ = Phase::ANSWERED;
        co_return _Chunk{ };   // the head is not body: nothing to show yet
      }
      co_return std::get<_Chunk>(*turn);
    }

    friend auto Send(Request request) -> asio::awaitable<TextReply>;
    friend auto Send(Request request, Encoding required) -> asio::awaitable<TextReply>;
    friend auto Send(Request request, UseBytesTag) -> asio::awaitable<BytesReply>;
    friend auto Send(Request request, UseStreamTag) -> TextStream;
    friend auto Send(Request request, Encoding required, UseStreamTag) -> TextStream;
    friend auto Send(Request request, UseBytesTag, UseStreamTag) -> BytesStream;

    Request                 request_ { };
    std::optional<Encoding> required_{ };
    std::optional<
        asio::experimental::coro<std::variant<Response, _Chunk>, void>> steps_{ };
    Phase                   phase_   { Phase::UNSENT };
  };

  // Untagged is awaited and atomic; tagged is not awaited and has done nothing.
  // No HEAD: a response to it announces a body that never comes, so the parser
  // has to be told to expect none -- behaviour rather than a name.
  auto Fetch(std::string url) -> asio::awaitable<TextReply>;
  auto Fetch(std::string url, Encoding required) -> asio::awaitable<TextReply>;
  auto Fetch(std::string url, UseBytesTag) -> asio::awaitable<BytesReply>;
  auto Fetch(std::string url, UseStreamTag) -> TextStream;
  auto Fetch(std::string url, Encoding required, UseStreamTag) -> TextStream;
  auto Fetch(std::string url, UseBytesTag, UseStreamTag) -> BytesStream;

  constexpr auto JSON_MEDIA_TYPE{ "application/json"sv };

  // The media type defaults to JSON: naming this overload is the caller
  // saying so, exactly as calling Fetch with no encoding says text.
  auto Post(std::string url, std::string body) -> asio::awaitable<TextReply>;
  auto Post(std::string url, std::string body, std::string content_type)
      -> asio::awaitable<TextReply>;
  auto Post(std::string url, std::string body, UseBytesTag)
      -> asio::awaitable<BytesReply>;
  auto Post(std::string url, std::string body, std::string content_type,
            UseBytesTag) -> asio::awaitable<BytesReply>;
  auto Post(std::string url, std::string body, UseStreamTag) -> TextStream;
  auto Post(std::string url, std::string body, std::string content_type,
            UseStreamTag) -> TextStream;

  auto Put(std::string url, std::string body) -> asio::awaitable<TextReply>;
  auto Put(std::string url, std::string body, std::string content_type)
      -> asio::awaitable<TextReply>;
  auto Put(std::string url, std::string body, UseBytesTag)
      -> asio::awaitable<BytesReply>;
  auto Put(std::string url, std::string body, std::string content_type,
           UseBytesTag) -> asio::awaitable<BytesReply>;
  auto Put(std::string url, std::string body, UseStreamTag) -> TextStream;
  auto Put(std::string url, std::string body, std::string content_type,
           UseStreamTag) -> TextStream;

  auto Delete(std::string url) -> asio::awaitable<TextReply>;
  auto Delete(std::string url, UseBytesTag) -> asio::awaitable<BytesReply>;
  auto Delete(std::string url, UseStreamTag) -> TextStream;
}

namespace oxbox::http
{
  using detail::fetch::BytesReply;
  using detail::fetch::BytesStream;
  using detail::fetch::CHARSET;
  using detail::fetch::Delete;
  using detail::fetch::Fetch;
  using detail::fetch::Header;
  using detail::fetch::FieldTable;
  using detail::fetch::JSON_MEDIA_TYPE;
  using detail::fetch::MediaType;
  using detail::fetch::Method;
  using detail::fetch::ParseMediaType;
  using detail::fetch::Payload;
  using detail::fetch::Post;
  using detail::fetch::Put;
  using detail::fetch::Reply;
  using detail::fetch::Request;
  using detail::fetch::Response;
  using detail::fetch::Send;
  using detail::fetch::Stream;
  using detail::fetch::TextReply;
  using detail::fetch::TextStream;
  using detail::fetch::UseBytes;
  using detail::fetch::UseBytesTag;
  using detail::fetch::UseStream;
  using detail::fetch::UseStreamTag;
}
