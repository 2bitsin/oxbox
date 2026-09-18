#include "oxbox/http/url.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace oxbox;

using http::Authority;
using http::Scheme;
using http::SplitUrl;

TEST(SplitUrl, HttpsWithoutAPortMeans443AndTls)
{
  auto const endpoint{ SplitUrl("https://api.example.com/v1/chat/completions") };
  EXPECT_EQ(endpoint.scheme, Scheme::HTTPS);
  EXPECT_EQ(endpoint.host, "api.example.com");
  EXPECT_EQ(endpoint.port, "443");
  EXPECT_EQ(endpoint.target, "/v1/chat/completions");
}

TEST(SplitUrl, HttpWithoutAPortMeans80AndNoTls)
{
  auto const endpoint{ SplitUrl("http://127.0.0.1/v1/models") };
  EXPECT_EQ(endpoint.scheme, Scheme::HTTP);
  EXPECT_EQ(endpoint.host, "127.0.0.1");
  EXPECT_EQ(endpoint.port, "80");
}

TEST(SplitUrl, AnExplicitPortWins)
{
  EXPECT_EQ(SplitUrl("http://127.0.0.1:8080/v1/models").port, "8080");
  EXPECT_EQ(SplitUrl("https://api.example.com:8443/v1").port, "8443");
}

TEST(SplitUrl, AMissingPathBecomesRoot)
{
  // there is no such thing as an empty request target on the wire
  EXPECT_EQ(SplitUrl("https://example.org").target, "/");
  EXPECT_EQ(SplitUrl("https://example.org:8443").target, "/");
  EXPECT_EQ(SplitUrl("https://example.org/").target, "/");
}

TEST(SplitUrl, TheJoinNormalizedBaseAndItsLeafBothSplitTheSameWay)
{
  // llama::Client strips a base's trailing slash and joins its leaves
  // with exactly one — both spellings must reach the same endpoint
  auto const joined{ SplitUrl("https://api.example.com/v1/models") };
  auto const trailing{ SplitUrl("https://api.example.com/v1/models/") };
  EXPECT_EQ(joined.host, trailing.host);
  EXPECT_EQ(joined.port, trailing.port);
  EXPECT_EQ(joined.target, "/v1/models");
  EXPECT_EQ(trailing.target, "/v1/models/");
}

TEST(SplitUrl, TheSchemeIsCaseInsensitive)
{
  EXPECT_EQ(SplitUrl("HTTPS://example.org/x").scheme, Scheme::HTTPS);
}

TEST(SplitUrl, AnIpv6LiteralLosesItsBracketsAndKeepsItsPort)
{
  auto const endpoint{ SplitUrl("http://[::1]:8080/v1") };
  EXPECT_EQ(endpoint.host, "::1");  // a resolver wants the bare address
  EXPECT_EQ(endpoint.port, "8080");
  EXPECT_EQ(endpoint.target, "/v1");
  EXPECT_EQ(SplitUrl("http://[::1]/v1").port, "80");
}

TEST(SplitUrl, AUrlThisClientCannotUseIsRefusedAtTheCall)
{
  EXPECT_THROW(SplitUrl("api.example.com/v1"), std::invalid_argument);
  EXPECT_THROW(SplitUrl("ftp://example.org/x"), std::invalid_argument);
  EXPECT_THROW(SplitUrl("https:///v1"), std::invalid_argument);
  EXPECT_THROW(SplitUrl(""), std::invalid_argument);
}

TEST(Authority, OmitsThePortExactlyWhenItIsTheSchemeDefault)
{
  EXPECT_EQ(Authority(SplitUrl("https://example.org/x")), "example.org");
  EXPECT_EQ(Authority(SplitUrl("http://example.org/x")), "example.org");
  // 80 under https is NOT a default, and a peer needs to be told
  EXPECT_EQ(Authority(SplitUrl("https://example.org:80/x")), "example.org:80");
  EXPECT_EQ(Authority(SplitUrl("http://example.org:8080/x")), "example.org:8080");
}

TEST(Authority, PutsAnIpv6LiteralBackInItsBrackets)
{
  EXPECT_EQ(Authority(SplitUrl("http://[::1]:8080/v1")), "[::1]:8080");
  EXPECT_EQ(Authority(SplitUrl("http://[::1]/v1")), "[::1]");
}
