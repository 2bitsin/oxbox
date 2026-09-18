#include "oxbox/http/asio.hpp"
#include "oxbox/http/fetch.hpp"

#include "oxbox/utilities/span.hpp"
#include "oxbox/utilities/text.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace oxbox::http::detail::fetch
{
  using namespace std::string_view_literals;

  namespace bh = beast::http;

  using detail::connection::Connection;
  using detail::delivery::Delivery;
  using detail::url::Authority;
  using detail::url::Endpoint;
  using detail::url::SplitUrl;
  using utilities::SpanCast;

  namespace
  {
    using OctetSteps = ByteSteps;

    // beast's spelling: 1.1 as major*10 + minor.
    constexpr unsigned HTTP_1_1{ 11u };

    constexpr char PARAMETER_SEPARATOR{ ';' };

    // a head that never terminates holds the connection open at this end
    constexpr std::size_t MAX_HEAD_BYTES{ 64u * 1024u };

    // comfortably above a path MTU's payload, so a message lands in one read
    constexpr std::size_t READ_BUFFER_BYTES{ 16u * 1024u };

    // a chunk larger than this is delivered in several turns
    constexpr std::size_t BODY_BUFFER_BYTES{ 16u * 1024u };

    auto Filled(asio::const_buffer buffer) noexcept -> std::span<std::byte const>
    {
      return { static_cast<std::byte const*>(buffer.data()), buffer.size() };
    }

    auto Room(asio::mutable_buffer buffer) noexcept -> std::span<std::byte>
    {
      return { static_cast<std::byte*>(buffer.data()), buffer.size() };
    }

    auto Text(beast::string_view text) noexcept -> std::string_view
    {
      return { text.data(), text.size() };
    }

    // Copied: the generator is lazy and may first run long after Fetch
    // returned, so nothing in its frame may reference the caller's.
    struct Plan {
      Endpoint                  endpoint  { };
      std::string               url       { };  // as given, for the messages
      beast::flat_buffer        wire      { };  // the rendered request
      std::optional<Encoding>   required  { };
      std::chrono::seconds      timeout   { };
      std::chrono::milliseconds heartbeat { };
    };

    // Order is the contract: caller headers go last and replace derived ones.
    auto Render(Request const& request, Endpoint const& endpoint,
                std::optional<Encoding> required) -> beast::flat_buffer
    {
      bh::request<bh::string_body> message{ request.method, endpoint.target,
                                            HTTP_1_1 };
      message.set(bh::field::host,   Authority(endpoint));
      message.set(bh::field::accept, "*/*"sv);
      if (required)
        message.set(bh::field::accept_charset, CharsetName(*required));
      if (request.payload) {
        message.set(bh::field::content_type, request.payload->content_type);
        message.body() = request.payload->body;
      }

      // Clearing first is what makes a caller's header replace a derived one;
      // inserting rather than setting is what lets a name written twice go out
      // twice. One pass doing both would erase the first of its own pair.
      for (Header const& header : request.headers)
        message.erase(header.name);
      for (Header const& header : request.headers)
        message.insert(header.name, header.value);

      // last, and past a caller's reach: the framing fields have to describe
      // the bytes actually written
      message.set(bh::field::connection, "close"sv);
      message.prepare_payload();

      beast::flat_buffer wire{ };
      beast::ostream(wire) << message;
      return wire;
    }

    auto Speak(asio::any_io_executor executor, Plan plan) -> OctetSteps
    {
      auto const deadline{ std::chrono::steady_clock::now() + plan.timeout };

      Connection connection{ executor, plan.endpoint };
      co_await connection.Open(deadline);
      co_await connection.Send(Filled(plan.wire.data()), deadline);

      bh::response_parser<bh::buffer_body> parser{ };
      parser.header_limit(MAX_HEAD_BYTES);
      // beast's body limit guards a parser that accumulates; this one hands
      // every chunk straight out and keeps none, so the transfer's bound is
      // the deadline, which a limit in bytes cannot express.
      parser.body_limit(boost::none);

      // beast reads a message out of one growing region (its documented
      // contract for a head split across reads), so the socket reads land
      // here too: one copy of a body byte on its way through, not two.
      beast::flat_buffer                       pending{ };
      std::array<std::byte, BODY_BUFFER_BYTES> decoded{ };

      Response                answer  { };
      std::optional<Delivery> delivery{ };  // engaged once the head is read

      for (;;) {
        auto const received{ co_await connection.Receive(
            Room(pending.prepare(READ_BUFFER_BYTES)), deadline, plan.heartbeat) };

        if (received.empty() && !connection.Closed()) {
          // A quiet turn, yielded whether or not the head has landed: waiting
          // for a head is exactly the wait a spinner exists to cover. The bare
          // Next absorbs it, Next(interval) reports it.
          co_yield std::span<std::byte const>{ };
          continue;
        }
        if (received.empty()) {
          // Whether the peer hanging up ends the body or truncates it is the
          // framing's question, and beast holds the framing.
          boost::system::error_code ending{ };
          parser.put_eof(ending);
          if (ending)
            throw TransportError{ std::format(
                "http fetch: {} closed the connection before the response was "
                "complete", plan.url) };
          break;
        }
        pending.commit(received.size());

        // One read can carry the head and several chunks at once.
        for (;;) {
          parser.get().body().data = decoded.data();
          parser.get().body().size = decoded.size();

          boost::system::error_code reading{ };
          pending.consume(parser.put(pending.data(), reading));
          auto const produced{ std::span<std::byte const>{ decoded }.first(
              decoded.size() - parser.get().body().size) };

          // Acted on the moment the head is whole and before one body byte
          // leaves: a declaration that cannot be honoured must fail now.
          if (parser.is_header_done() && !delivery) {
            auto const& head{ parser.get() };
            answer.status       = head.result_int();
            answer.reason       = std::string{ Text(head.reason()) };
            answer.content_type = ParseMediaType(Text(head[bh::field::content_type]));
            delivery = Delivery::For(plan.required,
                                     answer.content_type.Parameter(CHARSET), plan.url);
            // Taken here rather than at the end because the head is handed
            // over now: beast settled the framing when the header completed
            // and does not read them again. Chunked trailer fields are lost.
            answer.fields = std::move(static_cast<FieldTable&>(parser.get().base()));
            co_yield std::move(answer);
          }

          if (!produced.empty()) {
            auto const delivered{ delivery->Deliver(produced) };
            if (!delivered.empty())
              co_yield delivered;
          }

          if (reading == bh::error::need_buffer)
            continue;  // the chunk buffer filled: it is drained now, so go on
          if (reading == bh::error::need_more)
            break;     // beast wants bytes this read did not carry
          if (reading)
            throw TransportError{ std::format(
                "http fetch: {} did not answer in HTTP — {}", plan.url,
                reading.message()) };
          if (parser.is_done() || pending.size() == 0u)
            break;
        }

        if (parser.is_done())
          break;
      }

      // Whatever the delivery could not complete on the last chunk is owed
      // to the consumer before the response is.
      if (delivery)
        if (auto const tail{ delivery->Flush() }; !tail.empty())
          co_yield tail;
    }

    // Not a coroutine: a url this client cannot use must surface at the call,
    // not out of a resume in some other part of the program entirely.
    auto Begin(asio::any_io_executor executor, Request request,
               std::optional<Encoding> required) -> OctetSteps
    {
      auto endpoint{ SplitUrl(request.url) };
      auto wire{ Render(request, endpoint, required) };
      return Speak(executor, Plan{ .endpoint  = std::move(endpoint),
                                   .url       = std::move(request.url),
                                   .wire      = std::move(wire),
                                   .required  = required,
                                   .timeout   = request.timeout,
                                   .heartbeat = request.heartbeat });
    }

    // Nothing is converted here: delivery already made those bytes that
    // encoding, and a view stays valid only while the frame behind it is
    // suspended, which it is until this generator is resumed.
    auto Spell(OctetSteps octets) -> TextSteps
    {
      while (auto turn = co_await octets) {
        if (auto* const head = std::get_if<Response>(&*turn)) {
          co_yield std::move(*head);
          continue;
        }
        auto const letters{ SpanCast<char const>(
            std::get<std::span<std::byte const>>(*turn)) };
        co_yield std::string_view{ letters.data(), letters.size() };
      }
    }
  }

  auto MediaType::Parameter(std::string_view name) const
      -> std::optional<std::string_view>
  {
    auto const found{ parameters_.find(utilities::Lowered(name)) };
    if (found == parameters_.end())
      return std::nullopt;
    return std::string_view{ found->second };
  }

  auto ParseMediaType(std::string_view value) -> MediaType
  {
    MediaType media{ };

    auto const separator{ value.find(PARAMETER_SEPARATOR) };
    media.type_ = utilities::Lowered(utilities::Trimmed(value.substr(0u, separator)));
    if (separator == std::string_view::npos)
      return media;

    // The parameter grammar -- quoted strings, their escapes, and the ';'
    // that is data inside one -- is beast's (RFC 9110 §5.6.6); the folding
    // here is what makes the lookup case-insensitive.
    auto const tail{ value.substr(separator) };
    for (auto const& [name, text] :
         bh::param_list{ beast::string_view{ tail.data(), tail.size() } })
      // A repeated parameter is illegal (RFC 9110 §8.3.1) and the first wins.
      media.parameters_.try_emplace(utilities::Lowered(Text(name)), Text(text));

    return media;
  }

  auto StartBytes(asio::any_io_executor executor, Request request) -> ByteSteps
  {
    return Begin(executor, std::move(request), std::nullopt);
  }

  auto StartText(asio::any_io_executor executor, Request request,
                 Encoding required) -> TextSteps
  {
    return Spell(Begin(executor, std::move(request), required));
  }

  // Not coroutines and not awaited: they hand back a stream that has done
  // nothing at all yet.
  auto Send(Request request, Encoding required, UseStreamTag) -> TextStream
  {
    return TextStream{ std::move(request), required };
  }

  auto Send(Request request, UseBytesTag, UseStreamTag) -> BytesStream
  {
    return BytesStream{ std::move(request), std::nullopt };
  }

  // Atomic: the Reply is built only once the last chunk is in, so a transfer
  // that dies mid-body throws and hands over no half-bodied answer.
  namespace
  {
    template <typename _Body, typename _Stream>
    auto Drained(_Stream& stream) -> asio::awaitable<_Body>
    {
      _Body whole{ };
      while (!stream.Done())
        if (auto const chunk = co_await stream.Next())
          whole.insert(whole.end(), chunk->begin(), chunk->end());
      co_return whole;
    }
  }

  auto Send(Request request, Encoding required) -> asio::awaitable<TextReply>
  {
    auto stream{ TextStream{ std::move(request), required } };
    auto body{ co_await Drained<std::string>(stream) };
    co_return TextReply{ std::move(static_cast<Response&>(stream)),
                         std::move(body) };
  }

  auto Send(Request request, UseBytesTag) -> asio::awaitable<BytesReply>
  {
    auto stream{ BytesStream{ std::move(request), std::nullopt } };
    auto body{ co_await Drained<std::vector<std::byte>>(stream) };
    co_return BytesReply{ std::move(static_cast<Response&>(stream)),
                          std::move(body) };
  }

  auto Send(Request request) -> asio::awaitable<TextReply>
  { return Send(std::move(request), TEXT_ENCODING); }

  auto Send(Request request, UseStreamTag tag) -> TextStream
  { return Send(std::move(request), TEXT_ENCODING, tag); }

  auto Fetch(std::string url) -> asio::awaitable<TextReply>
  { return Send(Request{ .url = std::move(url) }); }

  auto Fetch(std::string url, Encoding required) -> asio::awaitable<TextReply>
  { return Send(Request{ .url = std::move(url) }, required); }

  auto Fetch(std::string url, UseBytesTag tag) -> asio::awaitable<BytesReply>
  { return Send(Request{ .url = std::move(url) }, tag); }

  auto Fetch(std::string url, UseStreamTag tag) -> TextStream
  { return Send(Request{ .url = std::move(url) }, tag); }

  auto Fetch(std::string url, Encoding required, UseStreamTag tag) -> TextStream
  { return Send(Request{ .url = std::move(url) }, required, tag); }

  auto Fetch(std::string url, UseBytesTag bytes, UseStreamTag tag) -> BytesStream
  { return Send(Request{ .url = std::move(url) }, bytes, tag); }

  namespace
  {
    auto Bodied(Method method, std::string url, std::string body,
                std::string content_type) -> Request
    {
      return Request{ .method  = method,
                      .url     = std::move(url),
                      .payload = Payload{ .content_type = std::move(content_type),
                                          .body         = std::move(body) } };
    }
  }

  auto Post(std::string url, std::string body) -> asio::awaitable<TextReply>
  { return Post(std::move(url), std::move(body), std::string{ JSON_MEDIA_TYPE }); }

  auto Post(std::string url, std::string body, std::string content_type)
      -> asio::awaitable<TextReply>
  { return Send(Bodied(Method::post, std::move(url), std::move(body), std::move(content_type))); }

  auto Post(std::string url, std::string body, UseBytesTag bytes)
      -> asio::awaitable<BytesReply>
  { return Post(std::move(url), std::move(body), std::string{ JSON_MEDIA_TYPE }, bytes); }

  auto Post(std::string url, std::string body, std::string content_type,
            UseBytesTag bytes) -> asio::awaitable<BytesReply>
  { return Send(Bodied(Method::post, std::move(url), std::move(body), std::move(content_type)),
                 bytes); }

  auto Post(std::string url, std::string body, UseStreamTag tag) -> TextStream
  { return Post(std::move(url), std::move(body), std::string{ JSON_MEDIA_TYPE }, tag); }

  auto Post(std::string url, std::string body, std::string content_type,
            UseStreamTag tag) -> TextStream
  { return Send(Bodied(Method::post, std::move(url), std::move(body), std::move(content_type)),
                 tag); }

  auto Put(std::string url, std::string body) -> asio::awaitable<TextReply>
  { return Put(std::move(url), std::move(body), std::string{ JSON_MEDIA_TYPE }); }

  auto Put(std::string url, std::string body, std::string content_type)
      -> asio::awaitable<TextReply>
  { return Send(Bodied(Method::put, std::move(url), std::move(body),
                       std::move(content_type))); }

  auto Put(std::string url, std::string body, UseBytesTag bytes)
      -> asio::awaitable<BytesReply>
  { return Put(std::move(url), std::move(body), std::string{ JSON_MEDIA_TYPE }, bytes); }

  auto Put(std::string url, std::string body, std::string content_type,
           UseBytesTag bytes) -> asio::awaitable<BytesReply>
  { return Send(Bodied(Method::put, std::move(url), std::move(body),
                       std::move(content_type)), bytes); }

  auto Put(std::string url, std::string body, UseStreamTag tag) -> TextStream
  { return Put(std::move(url), std::move(body), std::string{ JSON_MEDIA_TYPE }, tag); }

  auto Put(std::string url, std::string body, std::string content_type,
           UseStreamTag tag) -> TextStream
  { return Send(Bodied(Method::put, std::move(url), std::move(body),
                       std::move(content_type)), tag); }

  auto Delete(std::string url) -> asio::awaitable<TextReply>
  { return Send(Request{ .method = Method::delete_, .url = std::move(url) }); }

  auto Delete(std::string url, UseBytesTag bytes) -> asio::awaitable<BytesReply>
  { return Send(Request{ .method = Method::delete_, .url = std::move(url) }, bytes); }

  auto Delete(std::string url, UseStreamTag tag) -> TextStream
  { return Send(Request{ .method = Method::delete_, .url = std::move(url) }, tag); }
}
