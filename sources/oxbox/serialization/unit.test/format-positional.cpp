#include "oxbox/serialization/format-positional.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/utilities/unit.test/octets.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace
{
  namespace ser = oxbox::serialization;
  using F = ser::PositionalFormat;
  using Reader = F::Reader<ser::StringSource>;
  using oxbox::utilities::test::Octets;

#include "format-point.inc"

  struct Record
  {
    int number{ 1 };
    std::string text{ "x" };
    std::optional<int> absent;
    std::optional<int> present{ 2 };
    Point nested{ 3, 4, "y" };
    std::vector<int> array{ 5, 6 };
    auto operator==(Record const&) const -> bool = default;
  };

  constexpr auto reflect_scheme(Record*)
  {
    using T = Record;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"number", &T::number>,
      ::reflect::member_scheme<"text", &T::text>,
      ::reflect::member_scheme<"absent", &T::absent>,
      ::reflect::member_scheme<"present", &T::present>,
      ::reflect::member_scheme<"nested", &T::nested>,
      ::reflect::member_scheme<"array", &T::array>>{ };
  }

  auto Wire(auto const& bytes) -> std::string
  { return std::string{ F::AsChars(bytes) }; }

  static_assert([] {
    [[maybe_unused]] F::Cursor const cursor{ F::Bytes{} };
    return true;
  }());
  static_assert(ser::FormatTraits<F>::name == "positional");
  static_assert(ser::FormatTraits<F>::extensions.front() == ".bsp");
  static_assert(ser::FormatTraits<F>::separator.empty());
  static_assert(ser::FormatTraits<F>::openmode == std::ios::binary);
  static_assert(F::Writer<ser::StringSink>::PRESERVE_FIELD_SLOTS);
  static_assert(!ser::BinaryFormat::Writer<
                ser::StringSink>::PRESERVE_FIELD_SLOTS);
  static_assert(ser::FormatFromString("positional").has_value());
  static_assert(ser::FormatFromExtension(".bsp").has_value());
  static_assert(ser::HasScheme<Record>);
  static_assert(ser::HasScheme<Point>);
}

TEST(PositionalFormat, PinsRecordBytesAndSize)
{
  auto const expected{ Octets(
    5, 2, 31, 2, 6,
    2, 1,
    4, 2, 1, 'x',
    0,
    2, 2,
    5, 2, 10, 2, 3, 2, 3, 2, 4, 4, 2, 1, 'y',
    6, 2, 4, 2, 5, 2, 6) };
  Record const original;
  auto const wire{ ser::Serialize<F>(original) };
  EXPECT_EQ(wire, Wire(expected));
  EXPECT_EQ(wire.size(), 34u);
  auto const named{ ser::Serialize<ser::BinaryFormat>(original) };
  EXPECT_EQ(named.size(), 88u);
  EXPECT_EQ(named, Wire(Octets(
    5, 2, 85,
    4, 2, 6, 'n', 'u', 'm', 'b', 'e', 'r', 2, 1,
    4, 2, 4, 't', 'e', 'x', 't', 4, 2, 1, 'x',
    4, 2, 7, 'p', 'r', 'e', 's', 'e', 'n', 't', 2, 2,
    4, 2, 6, 'n', 'e', 's', 't', 'e', 'd',
    5, 2, 24,
    4, 2, 1, 'x', 2, 3,
    4, 2, 1, 'y', 2, 4,
    4, 2, 5, 'l', 'a', 'b', 'e', 'l', 4, 2, 1, 'y',
    4, 2, 5, 'a', 'r', 'r', 'a', 'y', 6, 2, 4, 2, 5, 2, 6)));
  EXPECT_LT(wire.size(), named.size());
  EXPECT_EQ((ser::Deserialize<F, Record>(wire)), original);
}

TEST(PositionalFormat, RoundTripsExistingReflectedPoint)
{
  Point const point{ -5, 42, "origin" };
  EXPECT_EQ((ser::Deserialize<F, Point>(ser::Serialize<F>(point))), point);
}

TEST(PositionalFormat, FieldsAdvanceRegardlessOfName)
{
  auto const wire{ ser::Serialize<F>(Record{}) };
  ser::StringSource source{ wire };
  Reader reader{ source };
  reader.EnterObject();
  EXPECT_TRUE(reader.FieldNames().empty());
  ASSERT_TRUE(reader.HasField("anything"));
  reader.EnterField("anything");
  EXPECT_EQ(reader.Path(), std::filesystem::path{ "anything" });
  EXPECT_EQ(reader.Read<std::int64_t>(), 1);
  reader.LeaveField();
  reader.EnterField("anything");
  EXPECT_EQ(reader.Read<std::string>(), "x");
  reader.LeaveField();
  reader.EnterField("absent");
  EXPECT_TRUE(reader.IsNull());
  reader.LeaveField();
  for (auto i{ 0 }; i < 3; ++i) {
    reader.EnterField("ignored");
    reader.LeaveField();
  }
  EXPECT_FALSE(reader.HasField("anything"));
  EXPECT_THROW(reader.EnterField("extra"), ser::MissingField);
  reader.LeaveObject();
  EXPECT_TRUE(reader.Path().empty());
}

TEST(PositionalFormat, TheThrownMessageNamesTheFieldAndTheItemItStandsIn)
{
  auto const wire{ ser::Serialize<F>(Record{}) };
  ser::StringSource source{ wire };
  Reader reader{ source };
  reader.EnterObject();
  for (auto skipped{ 0 }; skipped < 5; ++skipped) {
    reader.EnterField("ignored");
    reader.LeaveField();
  }
  reader.EnterField("array");
  reader.EnterArray();
  reader.EnterNext();
  try {
    static_cast<void>(reader.Read<std::string>());
    FAIL() << "an integer read as a string must throw";
  } catch (ser::TypeMismatch const& bad) {
    EXPECT_NE(std::string_view{ bad.what() }.find("array/[]"),
              std::string_view::npos) << bad.what();
  }
  reader.LeaveNext();
  reader.LeaveArray();
  reader.LeaveField();
}

TEST(PositionalFormat, NamesTheFieldAndTheItemAnErrorStandsIn)
{
  auto const wire{ ser::Serialize<F>(Record{}) };
  ser::StringSource source{ wire };
  Reader reader{ source };
  reader.EnterObject();
  for (auto skipped{ 0 }; skipped < 5; ++skipped) {
    reader.EnterField("ignored");
    reader.LeaveField();
  }
  reader.EnterField("array");
  reader.EnterArray();
  reader.EnterNext();
  EXPECT_EQ(reader.Path(), std::filesystem::path{ "array" } / "[]");
  reader.LeaveNext();
  reader.LeaveArray();
  reader.LeaveField();
  EXPECT_TRUE(reader.Path().empty());
}

TEST(PositionalFormat, CarriesOctetsAsOneLengthPrefixedRun)
{
  std::vector<std::byte> const octets{ std::byte{ 0x00 }, std::byte{ 0x7f },
                                       std::byte{ 0xff }, std::byte{ 0x80 } };
  auto const wire{ ser::Serialize<F>(octets) };
  EXPECT_EQ((ser::Deserialize<F, std::vector<std::byte>>(wire)), octets);
  // The string node's own shape, not a tagged integer per octet.
  EXPECT_EQ(wire.size(), ser::Serialize<F>(std::string{ "abcd" }).size());
  EXPECT_EQ((ser::Deserialize<F, std::array<std::byte, 4>>(wire)),
            (std::array{ std::byte{ 0x00 }, std::byte{ 0x7f },
                         std::byte{ 0xff }, std::byte{ 0x80 } }));
}

TEST(PositionalFormat, RefusesOctetsOfTheWrongTagOrCount)
{
  auto const wire{ ser::Serialize<F>(
    std::vector<std::byte>{ std::byte{ 1 }, std::byte{ 2 } }) };
  EXPECT_THROW((ser::Deserialize<F, std::array<std::byte, 3>>(wire)),
               ser::ParseError);
  EXPECT_THROW((ser::Deserialize<F, std::vector<std::byte>>(
                  ser::Serialize<F>(std::int64_t{ 7 }))), ser::TypeMismatch);
}

TEST(PositionalFormat, RejectsEveryTruncation)
{
  auto const wire{ ser::Serialize<F>(Record{}) };
  for (std::size_t cut{ 0 }; cut < wire.size(); ++cut) {
    EXPECT_THROW((ser::Deserialize<F, Record>(
      std::string_view{ wire }.substr(0, cut))), ser::ParseError) << cut;
  }
}

TEST(PositionalFormat, RejectsBadTagBeforeEnteringField)
{
  auto wire{ ser::Serialize<F>(Record{}) };
  wire[5] = static_cast<char>(0xff);
  ser::StringSource source{ wire };
  Reader reader{ source };
  EXPECT_THROW(reader.EnterObject(), ser::ParseError);
}

TEST(PositionalFormat, RejectsChildEscapingObjectBeforeReading)
{
  auto const wire{ Wire(Octets(5, 2, 5, 2, 1, 4, 2, 3, 'a', 'b', 'c')) };
  ser::StringSource source{ wire };
  Reader reader{ source };
  EXPECT_THROW(reader.EnterObject(), ser::ParseError);
}

TEST(PositionalFormat, RejectsChildEscapingArrayBeforeReading)
{
  auto const wire{ Wire(Octets(6, 2, 3, 4, 2, 3, 'a', 'b', 'c')) };
  ser::StringSource source{ wire };
  Reader reader{ source };
  reader.EnterArray();
  EXPECT_THROW(reader.EnterNext(), ser::ParseError);
}

TEST(PositionalFormat, RejectsCountsOutsidePayload)
{
  for (auto const& bytes : {
    Wire(Octets(5, 2, 2, 2, 1)),
    Wire(Octets(5, 2, 4, 2, 0, 2, 1)),
    Wire(Octets(5, 2, 1, 2, 0)),
    Wire(Octets(5, 2, 3, 2, 0xff, 0x7f)) }) {
    ser::StringSource source{ bytes };
    Reader reader{ source };
    EXPECT_THROW(reader.EnterObject(), ser::ParseError);
  }
}

TEST(PositionalFormat, RejectsOverlongLengthsAndVarints)
{
  for (auto const& bytes : {
    Wire(Octets(4, 2, 127)),
    Wire(Octets(4, 2, 0xff, 0xff, 0xff, 0xff, 0xff,
                0xff, 0xff, 0xff, 0xff, 2)) }) {
    ser::StringSource source{ bytes };
    Reader reader{ source };
    EXPECT_THROW(reader.Read<std::string>(), ser::ParseError);
  }
}

TEST(PositionalFormat, RegistryAndOptionalSlots)
{
  EXPECT_EQ(ser::FormatTraits<F>::name, "positional");
  EXPECT_EQ(ser::FormatTraits<F>::extensions.front(), ".bsp");
  EXPECT_TRUE(ser::FormatTraits<F>::separator.empty());
  EXPECT_EQ(ser::FormatTraits<F>::openmode, std::ios::binary);
  EXPECT_TRUE(std::holds_alternative<F>(
    *ser::FormatFromString("positional")));
  EXPECT_TRUE(std::holds_alternative<F>(*ser::FormatFromExtension(".bsp")));
  Record original;
  original.absent = 7;
  original.present.reset();
  EXPECT_EQ((ser::Deserialize<F, Record>(ser::Serialize<F>(original))),
            original);
}
