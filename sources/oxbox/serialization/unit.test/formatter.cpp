#include "oxbox/serialization/formatter.hpp"

#include <_buildutil/reflect.hpp>

#include <format>
#include <gtest/gtest.h>
#include <string>

namespace
{
  struct Greeting
  {
    friend constexpr auto reflect_scheme(Greeting*);

    std::string text;
    int count;
  };

  constexpr auto reflect_scheme(Greeting*)
  {
    using T = Greeting;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"text",  &T::text>,
      ::reflect::member_scheme<"count", &T::count>>{ };
  }
}

TEST(SerializationFormatter, DefaultIsXmlPretty)
{
  Greeting const g{ "hi", 3 };
  auto const out{ std::format("{}", g) };
  EXPECT_NE(out.find("<text>"),  std::string::npos);
  EXPECT_NE(out.find("hi"),      std::string::npos);
  EXPECT_NE(out.find("\n"),      std::string::npos); // pretty → multiline
}

TEST(SerializationFormatter, ExplicitJson)
{
  Greeting const g{ "hi", 3 };
  auto const out{ std::format("{:json}", g) };
  EXPECT_NE(out.find("\"text\""), std::string::npos);
  EXPECT_NE(out.find("\"hi\""),   std::string::npos);
  EXPECT_NE(out.find("3"),        std::string::npos);
}

TEST(SerializationFormatter, ExplicitYaml)
{
  Greeting const g{ "hi", 3 };
  auto const out{ std::format("{:yaml}", g) };
  EXPECT_NE(out.find("text:"),  std::string::npos);
  EXPECT_NE(out.find("count:"), std::string::npos);
}

TEST(SerializationFormatter, ExplicitXmlCompact)
{
  Greeting const g{ "hi", 3 };
  auto const out{ std::format("{:xml}", g) };
  EXPECT_NE(out.find("<text>"), std::string::npos);
  // compact XML shouldn't have indent between root and children
  EXPECT_EQ(out.find("\n  "), std::string::npos);
}

TEST(SerializationFormatter, UnknownSpecThrows)
{
  Greeting const g{ "hi", 3 };
  EXPECT_THROW((void)std::format("{:rot13}", g), std::format_error);
}

TEST(SerializationFormatter, EmbedsInLargerFormatString)
{
  Greeting const g{ "hi", 3 };
  auto const out{ std::format("BEFORE [{:json}] AFTER", g) };
  EXPECT_TRUE(out.starts_with("BEFORE ["));
  EXPECT_TRUE(out.ends_with("] AFTER"));
}
