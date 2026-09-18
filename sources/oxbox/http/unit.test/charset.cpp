// The charset guarantee: a body arrives in the encoding the caller asked for
// or the transfer is refused at the head -- bytes are never transcoded behind
// the caller's back, and a body this client cannot deliver costs one head.

#include "oxbox/http/unit.test/scripted-peer.hpp"

#include <gtest/gtest.h>

#include <exception>
#include <string>
#include <string_view>

using namespace oxbox;
using namespace oxbox::http::test;

TEST(FetchCharset, ADeclarationlessBodyIsDeliveredExactlyAsItArrived)
{
  // no charset parameter at all, so there is nothing to check the
  // requirement against
  Delivered got{ };
  auto const failure{ Exchange(
      { Head("application/json", 7u) + R"({"a":1})" }, Encoding::UTF8, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, R"({"a":1})");
}

TEST(FetchCharset, TheRequiredEncodingItselfPassesThroughUntouched)
{
  auto const body{ std::string{ "caf" } + std::string{ E_ACUTE_8 } };
  Delivered got{ };
  auto const failure{ Exchange(
      { Head("text/plain; charset=UTF-8", body.size()) + body }, Encoding::UTF8, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, body);
}

TEST(FetchCharset, AByteCompatibleDeclarationDeliversItsTopHalfBytesUNTOUCHED)
{
  // 0xE9 declared iso-8859-1 is U+00E9, which a transcode would spell as two
  // UTF-8 octets; the one byte the server sent is the one the caller gets
  auto const wire{ std::string{ "caf" } + std::string{ E_ACUTE_1 } };
  Delivered got{ };
  auto const failure{ Exchange(
      { Head("text/plain; charset=iso-8859-1", wire.size()) + wire },
      Encoding::UTF8, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, wire);
}

TEST(FetchCharset, AUsAsciiDeclarationIsPassedThroughToo)
{
  Delivered got{ };
  auto const failure{ Exchange(
      { Head("text/plain; charset=us-ascii", 5u) + "plain" }, Encoding::UTF8, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, "plain");
}

TEST(FetchCharset, ACharsetNameNobodyRecognisesIsPassedThroughAndNotRefused)
{
  Delivered got{ };
  auto const failure{ Exchange(
      { Head("text/plain; charset=shift_jis", 5u) + "bytes" }, Encoding::UTF8, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, "bytes");
}

TEST(FetchCharset, AWideCharsetIsRefusedAsUnimplementedBeforeTheFirstChunk)
{
  // the point of deciding at the head: a body this client cannot deliver
  // costs one head and nothing else
  Delivered got{ };
  auto const failure{ Exchange(
      { Head("text/plain; charset=utf-16le", 4u), "h\0i\0"s }, Encoding::UTF8, got) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), TransportError);
  try {
    std::rethrow_exception(failure);
  } catch (TransportError const& refused) {
    std::string_view const said{ refused.what() };
    EXPECT_NE(said.find("utf-16le"), std::string_view::npos) << said;
    EXPECT_NE(said.find("not implemented"), std::string_view::npos) << said;
  }
}

TEST(FetchCharset, AskingToBeGivenAWideEncodingIsRefusedAtTheHeadToo)
{
  // the server declares a charset that would have passed straight through,
  // and it is the encoding the caller asked for that stops the transfer
  Delivered got{ };
  auto const failure{ Exchange(
      { Head("text/plain; charset=utf-8", 5u) + "plain" }, Encoding::UTF16, got) };

  ASSERT_TRUE(failure);
  EXPECT_THROW(std::rethrow_exception(failure), TransportError);
  try {
    std::rethrow_exception(failure);
  } catch (TransportError const& refused) {
    std::string_view const said{ refused.what() };
    EXPECT_NE(said.find("utf-16"), std::string_view::npos) << said;
    EXPECT_NE(said.find("not implemented"), std::string_view::npos) << said;
  }
}

TEST(FetchCharset, AskingForNothingRunsNoCharsetLogicAtAll)
{
  // even the declaration that would have stopped the text door is not
  // looked at, because nothing was promised about it
  Delivered got{ };
  auto const failure{ OpaqueExchange(
      { Head("application/octet-stream; charset=utf-16le", 4u), "h\0i\0"s }, got) };

  ASSERT_FALSE(failure);
  EXPECT_EQ(got.body, "h\0i\0"s);
}
