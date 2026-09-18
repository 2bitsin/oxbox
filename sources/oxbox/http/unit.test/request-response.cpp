// What this client puts on the wire -- the target and the host, the framing
// fields, the caller's own headers, the verb -- and what it makes of the
// answer that comes back, failures included.

#include "oxbox/http/unit.test/scripted-peer.hpp"

#include <gtest/gtest.h>

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace oxbox;
using namespace oxbox::http::test;

TEST(FetchRequest, AGetCarriesTheOriginFormTargetAndTheHost)
{
  auto const wire{ Sent({ .method = http::Method::get, .url = "/v1/models" }) };
  EXPECT_TRUE(wire.starts_with("GET /v1/models HTTP/1.1\r\n")) << wire;
  EXPECT_NE(wire.find("Host: 127.0.0.1:"), std::string::npos) << wire;
}

TEST(FetchRequest, ABodyLessRequestSendsNoContentFieldsAtAll)
{
  // "Content-Length: 0" on a GET is a body some servers then wait for
  auto const wire{ Sent({ .method = http::Method::get, .url = "/v1/models" }) };
  EXPECT_EQ(wire.find("Content-Length"), std::string::npos) << wire;
  EXPECT_EQ(wire.find("Content-Type"), std::string::npos) << wire;
}

TEST(FetchRequest, ABodyBringsItsLengthItsTypeAndItselfInOneBuffer)
{
  auto const wire{ Sent({ .method  = http::Method::post,
                          .url     = "/v1/chat/completions",
                          .payload = http::Payload{
                              .content_type = "application/json",
                              .body         = R"({"model":"x"})" } }) };
  EXPECT_TRUE(wire.starts_with("POST /v1/chat/completions HTTP/1.1\r\n")) << wire;
  EXPECT_NE(wire.find("Content-Type: application/json\r\n"), std::string::npos) << wire;
  EXPECT_NE(wire.find("Content-Length: 13\r\n"), std::string::npos) << wire;
  EXPECT_TRUE(wire.ends_with("\r\n\r\n" + std::string{ R"({"model":"x"})" })) << wire;
}

TEST(FetchRequest, CredentialsAreJustHeadersAndNothingHereKnowsTheScheme)
{
  auto const with{ Sent({ .url     = "/x",
                          .headers = {{ "Authorization", "Bearer glpat-x" }} }) };
  EXPECT_NE(with.find("Authorization: Bearer glpat-x\r\n"), std::string::npos) << with;

  // no field at all, which is a different thing from an empty one
  auto const without{ Sent({ .url = "/x" }) };
  EXPECT_EQ(without.find("Authorization"), std::string::npos) << without;
}

TEST(FetchRequest, EveryRequestAsksThePeerToCloseTheConnection)
{
  EXPECT_NE(Sent({ .url = "/x" }).find("Connection: close\r\n"), std::string::npos);
}

TEST(FetchRequest, AWantedEncodingIsStatedOnTheWireAsWellAsEnforced)
{
  // RFC 9110 §12.5.2 deprecates the field and most servers ignore it; the
  // guarantee that backs it is ours either way
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  Delivered got{ };
  asio::co_spawn(loop, peer.Serve({ Head("text/plain", 2u) + "ok" }), asio::detached);
  asio::co_spawn(loop, CollectText({ .url = peer.Url() }, Encoding::UTF8, got),
                 asio::detached);
  loop.run();
  EXPECT_NE(peer.Asked().find("Accept-Charset: utf-8\r\n"), std::string_view::npos)
      << peer.Asked();

  // and a caller that wants octets says nothing at all
  EXPECT_EQ(Sent({ .url = "/x" }).find("Accept-Charset"), std::string::npos);
}

TEST(Headers, EveryOneGoesOnTheWireInTheOrderItWasGiven)
{
  auto const wire{ Sent({ .url     = "/x",
                          .headers = {{ "Authorization", "Bearer glpat-x" },
                                      { "X-Request-Id",  "abc123"        },
                                      { "X-Trace",       "on"            }} }) };
  EXPECT_NE(wire.find("Authorization: Bearer glpat-x\r\n"), std::string::npos) << wire;
  EXPECT_NE(wire.find("X-Request-Id: abc123\r\n"), std::string::npos) << wire;
  EXPECT_NE(wire.find("X-Trace: on\r\n"), std::string::npos) << wire;
}

TEST(Headers, ARepeatedNameIsSentAsManyTimesAsItWasWritten)
{
  // repeated fields are legal (RFC 9110 §5.2)
  auto const wire{ Sent({ .url     = "/x",
                          .headers = {{ "X-Tag", "one" },
                                      { "X-Tag", "two" }} }) };
  EXPECT_NE(wire.find("X-Tag: one\r\n"), std::string::npos) << wire;
  EXPECT_NE(wire.find("X-Tag: two\r\n"), std::string::npos) << wire;
}

TEST(Headers, ACallersHeaderReplacesOneThisModuleDerived)
{
  auto const wire{ Sent({ .url     = "/x",
                          .headers = {{ "Accept", "application/json" }} }) };
  EXPECT_NE(wire.find("Accept: application/json\r\n"), std::string::npos) << wire;
  EXPECT_EQ(wire.find("Accept: */*\r\n"), std::string::npos) << wire;
}

TEST(Headers, TheFramingFieldsAreNotTheCallersToSet)
{
  // Content-Length must describe the bytes actually written or this client
  // emits a malformed message, and "Connection: close" states what this
  // client then does rather than a preference it holds
  auto const wire{ Sent({ .method  = http::Method::post,
                          .url     = "/x",
                          .payload = http::Payload{
                              .content_type = "application/json",
                              .body         = R"({"a":1})" },
                          .headers = {{ "Content-Length", "9999"      },
                                      { "Connection",     "keep-alive" }} }) };
  EXPECT_NE(wire.find("Content-Length: 7\r\n"), std::string::npos) << wire;
  EXPECT_EQ(wire.find("9999"), std::string::npos) << wire;
  EXPECT_NE(wire.find("Connection: close\r\n"), std::string::npos) << wire;
  EXPECT_EQ(wire.find("keep-alive"), std::string::npos) << wire;
}

TEST(Verbs, EachSetsItsMethodAndCarriesWhatThatVerbNeeds)
{
  asio::io_context loop{ };
  ScriptedPeer put{ loop }, gone{ loop };
  auto const answer{ Head("text/plain", 2u) + "ok" };
  asio::co_spawn(loop, put.Serve({ answer }), asio::detached);
  asio::co_spawn(loop, gone.Serve({ answer }), asio::detached);

  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    co_await http::Put(put.Url(), "a,b\n1,2", "text/csv");
    co_await http::Delete(gone.Url());
  }()) };
  ASSERT_FALSE(failure);

  auto const wrote{ put.Asked() };
  EXPECT_EQ(wrote.find("PUT / HTTP/1.1"), 0u) << wrote;
  EXPECT_NE(wrote.find("Content-Type: text/csv\r\n"), std::string_view::npos) << wrote;
  EXPECT_TRUE(wrote.ends_with("a,b\n1,2")) << wrote;

  auto const removed{ gone.Asked() };
  EXPECT_EQ(removed.find("DELETE / HTTP/1.1"), 0u) << removed;
  EXPECT_EQ(removed.find("Content-Length"), std::string_view::npos) << removed;
}

TEST(Verbs, SendIsTheSameTransferWithTheWholeRequestSpelledOut)
{
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::string body{ };

  // built outside the coroutine: a braced vector-of-aggregate directly inside
  // a coroutine frame ICEs gcc 16.1 (gimplify.cc:841)
  http::Request request{ .method  = http::Method::post,
                         .url     = peer.Url(),
                         .payload = http::Payload{
                             .content_type = "application/json",
                             .body         = R"({"a":1})" },
                         .headers = {{ "Authorization", "Bearer glpat-x" }},
                         .timeout = 30s };

  asio::co_spawn(loop, peer.Serve({ Head("text/plain", 2u) + "ok" }), asio::detached);
  auto const failure{ Thrown(loop, [&, request = std::move(request)]() mutable
                                   -> asio::awaitable<void> {
    body = (co_await http::Send(std::move(request))).body;
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(body, "ok");
  auto const wire{ peer.Asked() };
  EXPECT_EQ(wire.find("POST "), 0u) << wire;
  EXPECT_NE(wire.find("Authorization: Bearer glpat-x\r\n"),
            std::string_view::npos) << wire;
}

TEST(FetchDoors, AUrlThisClientCannotUseIsRefusedOutOfTheOneAwait)
{
  // before any socket is touched
  asio::io_context loop{ };
  Delivered got{ };
  auto const failure{ Thrown(loop, CollectBytes({ .url = "ftp://example.org/x" }, got)) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), std::invalid_argument);
}

TEST(FetchResponse, TheStatusAndItsReasonPhraseSurviveToTheCaller)
{
  Delivered got{ };
  auto const failure{ OpaqueExchange(
      { "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 4\r\n\r\nnope" }, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.status_line, "502 Bad Gateway");
  EXPECT_EQ(got.body, "nope");
}

TEST(FetchResponse, AChunkedBodyArrivesDecodedAndItsFramingNeverShows)
{
  // the pieces cut the chunk grammar where a network would: through a size
  // line, and between a chunk and its terminator
  Delivered got{ };
  auto const failure{ OpaqueExchange(
      { "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel",
        "lo\r\n6",
        "\r\n world\r\n0\r\n\r\n" }, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, "hello world");
}

TEST(FetchResponse, ABodyThatRunsToTheCloseEndsWhereTheConnectionDoes)
{
  // no Content-Length and no chunked framing: the close is the ending
  Delivered got{ };
  auto const failure{ OpaqueExchange(
      { "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n",
        "data: hi\n\n" }, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, "data: hi\n\n");
}

TEST(FetchResponse, ABodyCutBeforeItsAnnouncedLengthIsATransportFailure)
{
  // the same close, under a framing that promised more
  Delivered got{ };
  auto const failure{ OpaqueExchange(
      { "HTTP/1.1 200 OK\r\nContent-Length: 99\r\n\r\nshort" }, got) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), TransportError);
}

TEST(FetchResponse, APeerThatIsNotSpeakingHttpIsATransportFailure)
{
  Delivered got{ };
  auto const failure{ OpaqueExchange({ R"({"error":"nope"})" }, got) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), TransportError);
}

TEST(FetchResponse, TheDeclaredMediaTypeAndTheFieldsReachTheCaller)
{
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::optional<http::Response> answer{ };

  // X-Request-Id is a field this client derives nothing from and could not
  // invent, so reading it back is what proves the whole table crossed out of
  // the parser rather than being rebuilt from the fields fetch.cpp reads.
  asio::co_spawn(loop, peer.Serve(
      { "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Content-Length: 2\r\n"
        "X-Request-Id: 7f3a\r\n"
        "\r\nok"s }), asio::detached);
  auto const failure{ Thrown(loop, CollectHead({ .url = peer.Url() }, answer)) };

  ASSERT_FALSE(failure);
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->content_type.Type(), "text/event-stream");
  EXPECT_EQ(answer->content_type.Parameter(http::CHARSET), "utf-8");
  // case-insensitive as RFC 9110 §5.1 requires
  EXPECT_EQ((*answer)["content-length"], "2");
  EXPECT_EQ((*answer)["CoNtEnT-LeNgTh"], "2");
  EXPECT_EQ((*answer)["x-request-id"], "7f3a");
  EXPECT_FALSE((*answer)["x-nothing-here"]);
}

TEST(FetchWhole, AStallBetweenHeadAndBodyIsInvisibleToTheWholeResponseDoor)
{
  Delivered got{ };
  auto const failure{ OpaqueExchange({ Head("text/plain", 2u), "ok" }, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, "ok");
}

TEST(FetchWhole, APeerDyingMidBodyThrowsAndHandsBackNoHalfResponse)
{
  // a peer that announces eleven bytes, sends five and hangs up must not
  // produce an answer carrying those five: a caller that got a Reply may
  // trust its body entirely
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  bool answered{ false };

  asio::co_spawn(loop, peer.Serve(
      { "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhello" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto reply{ co_await http::Fetch(peer.Url()) };
    answered = true;   // must never be reached
    EXPECT_EQ(reply.body, "");
  }()) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), TransportError);
  EXPECT_FALSE(answered);   // no partial response escaped the await
}
