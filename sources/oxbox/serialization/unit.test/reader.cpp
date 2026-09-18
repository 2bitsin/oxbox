#include "oxbox/serialization/reader.hpp"

#include "oxbox/serialization/delimited-string.hpp"
#include "oxbox/serialization/serializable.hpp"
#include "oxbox/serialization/writer.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
  using namespace std::string_view_literals;

  using oxbox::serialization::DelimitedString;
  using oxbox::serialization::Reader;
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

  auto WriteThenFetch(std::vector<int> const& ns, std::string_view format)
    -> std::vector<int>
  {
    std::ostringstream out;
    auto const writer{ Writer::Make<Alpha>(format) };
    auto       hint  { WriteOrderHint::FIRST };
    for (auto const n : ns)
    {
      writer->Write(out, Alpha{ n }, hint);
      hint = WriteOrderHint::NEXT;
    }

    std::istringstream in{ std::move(out).str() };
    auto const reader{ Reader::Make<Alpha>(format) };
    std::vector<int> got;
    for (Alpha record{}; reader->Read(in, record); )
      got.push_back(record.n);
    return got;
  }
}

TEST(Reader, RoundTripsRecordsThroughJson)
{
  EXPECT_EQ(WriteThenFetch({ 1, 2, 3 }, "json"), (std::vector{ 1, 2, 3 }));
}

TEST(Reader, RoundTripsRecordsThroughYaml)
{
  EXPECT_EQ(WriteThenFetch({ 4, 5, 6 }, "yaml"), (std::vector{ 4, 5, 6 }));
}

TEST(Reader, MultiReaderDispatchesOnRecordType)
{
  std::ostringstream out;
  auto const writer{ Writer::Make<Alpha, Beta>("json") };
  writer->Write(out, Alpha{ 7 });
  writer->Write(out, Beta{ "hi" }, WriteOrderHint::NEXT);

  std::istringstream in{ std::move(out).str() };
  auto const reader{ Reader::Make<Alpha, Beta>("json") };

  Alpha alpha{};
  Beta  beta{};
  ASSERT_TRUE(reader->Read(in, alpha));
  ASSERT_TRUE(reader->Read(in, beta));
  EXPECT_EQ(alpha.n, 7);
  EXPECT_EQ(beta.s, "hi");
}

TEST(Reader, ReadsLiteralJsonlStream)
{
  std::istringstream in{ R"({"n":10})" "\n" R"({"n":20})" };
  auto const reader{ Reader::Make<Alpha>("json") };

  std::vector<int> got;
  for (Alpha record{}; reader->Read(in, record); )
    got.push_back(record.n);
  EXPECT_EQ(got, (std::vector{ 10, 20 }));
}

TEST(Reader, EmptyStreamYieldsNoRecord)
{
  std::istringstream in{ "" };
  auto const reader{ Reader::Make<Alpha>("json") };
  Alpha record{};
  EXPECT_FALSE(reader->Read(in, record));
}

TEST(Reader, FormatsListsJsonAndYaml)
{
  auto const formats{ Reader::Formats() };
  EXPECT_TRUE(std::ranges::contains(formats, "json"sv));
  EXPECT_TRUE(std::ranges::contains(formats, "yaml"sv));
}

TEST(Reader, UnknownFormatReturnsNull)
{
  EXPECT_EQ(Reader::Make<Alpha>("toml"), nullptr);
}

// ── DelimitedString ───────────────────────────────────────────────────

TEST(Reader, DelimitedStringReadsTerminatedLines)
{
  std::istringstream in{ "alpha\nbeta\ngamma" };
  auto const reader{ Reader::Make<DelimitedString<"\n">>("json") };

  std::vector<std::string> got;
  for (std::string line; reader->Read(in, line); )
    got.push_back(line);
  EXPECT_EQ(got, (std::vector<std::string>{ "alpha", "beta", "gamma" }));
}

TEST(Reader, DelimitedStringRoundTrip)
{
  std::vector<std::string> const lines{ "alpha", "beta", "gamma" };

  std::ostringstream out;
  auto const writer{ Writer::Make<DelimitedString<"\n">>("json") };
  for (std::size_t i{ 0u }; i < lines.size(); ++i)
  {
    auto const hint{ [&]{
      if (i + 1u == lines.size()) return WriteOrderHint::LAST;
      if (i == 0u)                return WriteOrderHint::FIRST;
      return WriteOrderHint::NEXT;
    }() };
    writer->Write(out, lines[i], hint);
  }

  std::istringstream in{ std::move(out).str() };
  auto const reader{ Reader::Make<DelimitedString<"\n">>("json") };
  std::vector<std::string> got;
  for (std::string line; reader->Read(in, line); )
    got.push_back(line);
  EXPECT_EQ(got, lines);
}

TEST(Reader, MixedPackDispatchesByArgument)
{
  std::ostringstream out;
  auto const writer{ Writer::Make<Alpha, DelimitedString<"\n">>("json") };
  writer->Write(out, Alpha{ 42 });
  writer->Write(out, std::string{ "line" }, WriteOrderHint::LAST);

// 
  std::ostringstream out2;
  auto const writer2{ Writer::Make<Alpha, DelimitedString<"\n">>("json") };
  writer2->Write(out2, std::string{ "preamble" });                // delim First → "preamble\n"
  writer2->Write(out2, Alpha{ 42 }, WriteOrderHint::LAST);        // record Last  → leading sep ignored? Hmm

// 
  std::istringstream in{ "preamble\n" R"({"n":42})" };
  auto const reader{ Reader::Make<Alpha, DelimitedString<"\n">>("json") };
  std::string line{};
  Alpha       record{};
  ASSERT_TRUE(reader->Read(in, line));
  ASSERT_TRUE(reader->Read(in, record));
  EXPECT_EQ(line, "preamble");
  EXPECT_EQ(record.n, 42);
}
