#include "oxbox/serialization/writer.hpp"

#include "oxbox/serialization/delimited-string.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
  using namespace std::string_view_literals;

  using oxbox::serialization::DelimitedString;
  using oxbox::serialization::WriteOrderHint;
  using oxbox::serialization::Writer;

  struct Alpha { friend constexpr auto reflect_scheme(Alpha*); int         n; };
  struct Beta  { friend constexpr auto reflect_scheme(Beta*);  std::string s; };

  constexpr auto reflect_scheme(Alpha*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"n", &Alpha::n>>{ };
  }

  constexpr auto reflect_scheme(Beta*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"s", &Beta::s>>{ };
  }
}

TEST(Writer, SingleTypeJsonlSeparatesWithNewline)
{
  auto const writer{ Writer::Make<Alpha>("json") };
  ASSERT_NE(writer, nullptr);

  std::ostringstream out;
  writer->Write(out, Alpha{ 1 });
  writer->Write(out, Alpha{ 2 }, WriteOrderHint::NEXT);
  EXPECT_EQ(out.str(), R"({"n":1})" "\n" R"({"n":2})");
}

TEST(Writer, FirstRecordGetsNoSeparator)
{
  auto const writer{ Writer::Make<Alpha>("json") };
  std::ostringstream out;
  writer->Write(out, Alpha{ 9 });
  EXPECT_EQ(out.str(), R"({"n":9})");
}

TEST(Writer, LastBehavesLikeNextForRecords)
{
// 
  auto const writer{ Writer::Make<Alpha>("json") };
  std::ostringstream out;
  writer->Write(out, Alpha{ 1 });
  writer->Write(out, Alpha{ 2 }, WriteOrderHint::NEXT);
  writer->Write(out, Alpha{ 3 }, WriteOrderHint::LAST);
  EXPECT_EQ(out.str(), R"({"n":1})" "\n" R"({"n":2})" "\n" R"({"n":3})");
}

TEST(Writer, MultiTypeDispatchesOnRecordType)
{
  auto const writer{ Writer::Make<Alpha, Beta>("json") };
  ASSERT_NE(writer, nullptr);

  std::ostringstream out;
  writer->Write(out, Alpha{ 7 });
  writer->Write(out, Beta{ "hi" }, WriteOrderHint::NEXT);
  EXPECT_EQ(out.str(), R"({"n":7})" "\n" R"({"s":"hi"})");
}

TEST(Writer, YamlSeparatesDocumentsWithMarker)
{
  auto const writer{ Writer::Make<Alpha>("yaml") };
  ASSERT_NE(writer, nullptr);

  std::ostringstream out;
  writer->Write(out, Alpha{ 1 });
  writer->Write(out, Alpha{ 2 }, WriteOrderHint::NEXT);
  EXPECT_NE(out.str().find("---"), std::string::npos);
}

TEST(Writer, UnknownFormatReturnsNull)
{
  EXPECT_EQ(Writer::Make<Alpha>("toml"), nullptr);
}

TEST(Writer, FormatsListsJsonAndYaml)
{
  auto const formats{ Writer::Formats() };
  EXPECT_TRUE(std::ranges::contains(formats, "json"sv));
  EXPECT_TRUE(std::ranges::contains(formats, "yaml"sv));
}

TEST(Writer, EveryAdvertisedFormatBuildsAWriter)
{
  for (auto const format : Writer::Formats())
    EXPECT_NE(Writer::Make<Alpha>(format), nullptr);
}

// ── DelimitedString ───────────────────────────────────────────────────

TEST(Writer, DelimitedStringWritesTerminator)
{
  auto const writer{ Writer::Make<DelimitedString<"\n">>("json") };
  ASSERT_NE(writer, nullptr);

  std::ostringstream out;
  writer->Write(out, std::string{ "alpha" });                          // First
  writer->Write(out, std::string{ "beta"  }, WriteOrderHint::NEXT);
  writer->Write(out, std::string{ "gamma" }, WriteOrderHint::LAST);
  EXPECT_EQ(out.str(), "alpha\nbeta\ngamma");
}

TEST(Writer, DelimitedStringWithoutLastKeepsTrailingDelimiter)
{
  auto const writer{ Writer::Make<DelimitedString<"\n">>("json") };
  std::ostringstream out;
  writer->Write(out, std::string{ "alpha" });
  writer->Write(out, std::string{ "beta"  }, WriteOrderHint::NEXT);
  writer->Write(out, std::string{ "gamma" }, WriteOrderHint::NEXT);
  EXPECT_EQ(out.str(), "alpha\nbeta\ngamma\n");
}

TEST(Writer, DelimitedStringMultiCharDelim)
{
  auto const writer{ Writer::Make<DelimitedString<"\r\n">>("json") };
  std::ostringstream out;
  writer->Write(out, std::string{ "a" });
  writer->Write(out, std::string{ "b" }, WriteOrderHint::LAST);
  EXPECT_EQ(out.str(), "a\r\nb");
}

TEST(Writer, MixedPackDispatchesByArgument)
{
  auto const writer{ Writer::Make<Alpha, DelimitedString<"\n">>("json") };
  ASSERT_NE(writer, nullptr);

  std::ostringstream out;
  writer->Write(out, Alpha{ 42 });                                 // record, First
  writer->Write(out, std::string{ "line" }, WriteOrderHint::LAST); // delim, Last → no trailing
  EXPECT_EQ(out.str(), R"({"n":42})" "line");
}
