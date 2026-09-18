// The short doors: a URL alone is the whole API for the common GET, a URL and
// a body for a POST, and every deviation from that is one named tag in last
// position.

#include "oxbox/http/unit.test/scripted-peer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>

using namespace oxbox;
using namespace oxbox::http::test;

TEST(FetchSimple, AUrlAloneIsTheWholeApiForTheCommonCase)
{
  // GET, no credential, no body, a sane timeout and UTF-8 text are all
  // defaults, so none of them is written here
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::string body{ };
  int status{ };

  asio::co_spawn(loop, peer.Serve(
      { Head("text/plain; charset=utf-8", 11u) + "hello world" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto reply{ co_await http::Fetch(peer.Url()) };
    status = reply.status;
    body   = std::move(reply.body);
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(status, 200);
  EXPECT_EQ(body, "hello world");
}

TEST(FetchSimple, AUrlAloneStreamsTooWhenTheTagAsksForIt)
{
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::string body{ };

  asio::co_spawn(loop, peer.Serve(
      { "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n",
        "6\r\n world\r\n0\r\n\r\n" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto stream{ http::Fetch(peer.Url(), http::UseStream) };
    while (!stream.Done())
      if (auto const chunk = co_await stream.Next()) body += *chunk;
    EXPECT_EQ(stream.status, 200);
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(body, "hello world");
}

TEST(FetchSimple, EveryDeviationIsATagAndTheyCompose)
{
  // text/whole is bare, and each deviation is one named tag in last position
  asio::io_context loop{ };
  ScriptedPeer text{ loop }, bytes{ loop }, streamed{ loop };
  std::string got_text{ }, got_streamed{ };
  std::size_t got_bytes{ 0u };

  auto const answer{ Head("text/plain", 2u) + "ok" };
  asio::co_spawn(loop, text.Serve({ answer }), asio::detached);
  asio::co_spawn(loop, bytes.Serve({ answer }), asio::detached);
  asio::co_spawn(loop, streamed.Serve({ answer }), asio::detached);

  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    got_text  = (co_await http::Fetch(text.Url())).body;             // text, whole
    got_bytes = (co_await http::Fetch(bytes.Url(), http::UseBytes)).body.size();
    auto stream{ http::Fetch(streamed.Url(), http::UseStream) };     // text, in pieces
    while (!stream.Done())
      if (auto const chunk = co_await stream.Next()) got_streamed += *chunk;
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got_text, "ok");
  EXPECT_EQ(got_bytes, 2u);
  EXPECT_EQ(got_streamed, "ok");
}

TEST(Post, AUrlAndABodyArePostedAsJsonAndAnsweredAsText)
{
  // the media type is not written because application/json is what naming
  // this overload means
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::string got{ };

  asio::co_spawn(loop, peer.Serve({ Head("application/json", 4u) + "true" }),
                 asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    got = (co_await http::Post(peer.Url(), R"({"model":"x"})")).body;
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got, "true");

  auto const wire{ peer.Asked() };
  EXPECT_EQ(wire.find("POST "), 0u) << wire;
  EXPECT_NE(wire.find("Content-Type: application/json\r\n"),
            std::string_view::npos) << wire;
  EXPECT_NE(wire.find("Content-Length: 13\r\n"), std::string_view::npos) << wire;
  EXPECT_TRUE(wire.ends_with(R"({"model":"x"})")) << wire;
}

TEST(Post, ABodyThatIsNotJsonSaysSoInTheSameBreath)
{
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };

  asio::co_spawn(loop, peer.Serve({ Head("text/plain", 2u) + "ok" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    co_await http::Post(peer.Url(), "a,b\n1,2", "text/csv");
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_NE(peer.Asked().find("Content-Type: text/csv\r\n"),
            std::string_view::npos) << peer.Asked();
}

TEST(Post, TheStreamTagComposesExactlyAsItDoesOnFetch)
{
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::string body{ };

  asio::co_spawn(loop, peer.Serve(
      { "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n",
        "data: hi\n\n" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto stream{ http::Post(peer.Url(), R"({"stream":true})", http::UseStream) };
    EXPECT_FALSE(stream.Answered());        // lazy, exactly as Fetch's is
    while (!stream.Done())
      if (auto const chunk = co_await stream.Next()) body += *chunk;
    EXPECT_EQ(stream.status, 200);
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(body, "data: hi\n\n");
  EXPECT_EQ(peer.Asked().find("POST "), 0u);
}

TEST(Post, ASizedUploadMayStillHaveItsAnswerStreamed)
{
  // the cell live SSE lives in: UseStream says nothing about the upload,
  // only about the response
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::string body{ };

  asio::co_spawn(loop, peer.Serve(
      { "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n",
        "data: one\n\n", "data: two\n\n" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto stream{ http::Post(peer.Url(), R"({"stream":true})",
                            "application/json", http::UseStream) };
    while (!stream.Done())
      if (auto const chunk = co_await stream.Next()) body += *chunk;
    EXPECT_EQ(stream.status, 200);
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(body, "data: one\n\ndata: two\n\n");

  auto const wire{ peer.Asked() };
  EXPECT_EQ(wire.find("POST "), 0u) << wire;
  EXPECT_NE(wire.find("Content-Length: 15\r\n"), std::string_view::npos) << wire;
  EXPECT_EQ(wire.find("Transfer-Encoding"), std::string_view::npos) << wire;
}
