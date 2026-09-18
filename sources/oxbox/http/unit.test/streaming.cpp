// The streamed door: chunks handed over one at a time with the head landing
// before the first of them, and the heartbeat that keeps ticking while the
// socket says nothing at all.

#include "oxbox/http/unit.test/scripted-peer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <exception>
#include <string>

using namespace oxbox;
using namespace oxbox::http::test;

TEST(FetchStream, AnIntervalTicksThroughAStallBetweenHeadAndBody)
{
  // a tick is nullopt while Done() still says false -- the same nullopt the
  // end uses, told apart by the one question the loop already asks
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::size_t ticks{ 0u };
  std::string body{ };

  asio::co_spawn(loop, peer.Serve({ Head("text/plain", 2u), "ok" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto stream{ http::Send({ .url = peer.Url(), .heartbeat = HEARTBEAT },
                             http::UseStream) };
    EXPECT_FALSE(stream.Answered());
    while (!stream.Done()) {
      auto const chunk{ co_await stream.Next(TICK) };
      if (chunk)
        body += *chunk;
      else if (!stream.Done())
        ++ticks;            // nullopt but still open: nothing yet
    }
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(body, "ok");
  EXPECT_GT(ticks, 0u);   // the quiet was heard
  EXPECT_EQ(peer.Asked().find("GET "), 0u);   // and it really did fetch
}

TEST(FetchStream, TheTaggedFormSendsNothingUntilTheFirstNext)
{
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };

  asio::co_spawn(loop, peer.Serve({ Head("text/plain", 2u) + "ok" }), asio::detached);
  auto stream{ http::Fetch(peer.Url(), http::UseStream) };
  EXPECT_FALSE(stream.Answered());
  EXPECT_FALSE(stream.Done());
  loop.poll();                       // let anything that would run, run
  EXPECT_TRUE(peer.Asked().empty());
}

TEST(FetchStream, ChunksArriveOneByOneAndTheHeadLandsBeforeThem)
{
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::string body{ };
  int status{ };

  asio::co_spawn(loop, peer.Serve(
      { "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n",
        "6\r\n world\r\n0\r\n\r\n" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto stream{ http::Fetch(peer.Url(), http::UseStream) };
    while (!stream.Done())
      if (auto const chunk = co_await stream.Next()) {
        EXPECT_TRUE(stream.Answered());   // the head precedes every chunk
        status = stream.status;
        body += *chunk;
      }
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(status, 200);
  EXPECT_EQ(body, "hello world");
}

TEST(FetchStream, AWideCharsetIsRefusedBeforeAnyChunkIsEverHandedOver)
{
  // the half the whole-body doors cannot show: a consumer driving the stream
  // by hand is never given one byte of a body it could not have read
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  bool handed{ false };

  asio::co_spawn(loop, peer.Serve(
      { Head("text/plain; charset=utf-16le", 4u), "h\0i\0"s }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto stream{ http::Fetch(peer.Url(), http::UseStream) };
    while (!stream.Done())
      if (auto const chunk = co_await stream.Next())
        handed = true;
  }()) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), TransportError);
  EXPECT_FALSE(handed);
}

TEST(FetchStream, AnIntervalTicksWhileTheHeadItselfIsStillBeingWaitedFor)
{
  // the peer says nothing at all for a while, and the spinner must turn
  // during that, not merely between body chunks once the server has answered
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::size_t before_head{ 0u };

  asio::co_spawn(loop, peer.Serve({ Head("text/plain", 2u) + "ok" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto stream{ http::Send({ .url = peer.Url(), .heartbeat = HEARTBEAT },
                             http::UseStream) };
    while (!stream.Done()) {
      auto const chunk{ co_await stream.Next(TICK) };
      if (!chunk && !stream.Done() && !stream.Answered())
        ++before_head;   // a tick, and no head yet
    }
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_GT(before_head, 0u);
}

TEST(FetchStream, AnExhaustedStreamKeepsSayingItIsDoneRatherThanFailing)
{
  // never a resumed-dead-generator failure from the plumbing underneath,
  // and never a different answer the second time
  asio::io_context loop{ };
  ScriptedPeer peer{ loop };
  std::string rest{ "unset" };

  asio::co_spawn(loop, peer.Serve({ Head("text/plain", 2u) + "ok" }), asio::detached);
  auto const failure{ Thrown(loop, [&]() -> asio::awaitable<void> {
    auto stream{ http::Fetch(peer.Url(), http::UseStream) };
    while (!stream.Done())
      if (auto const chunk = co_await stream.Next()) rest = *chunk;
    EXPECT_EQ(rest, "ok");
      EXPECT_FALSE((co_await stream.Next()).has_value());
    EXPECT_FALSE((co_await stream.Next(TICK)).has_value());
    EXPECT_TRUE(stream.Done());
  }()) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(rest, "ok");
}
