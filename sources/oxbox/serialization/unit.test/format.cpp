// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/format-binary.hpp"
#include "oxbox/serialization/format-json.hpp"
#include "oxbox/serialization/format-positional.hpp"
#include "oxbox/serialization/format-xml.hpp"
#include "oxbox/serialization/format-yaml.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>


static_assert(oxbox::serialization::Format<oxbox::serialization::JsonFormat>);
static_assert(oxbox::serialization::Format<oxbox::serialization::YamlFormat>);
static_assert(oxbox::serialization::Format<oxbox::serialization::XmlFormat>);
static_assert(oxbox::serialization::Format<oxbox::serialization::XmlPrettyFormat>);
static_assert(oxbox::serialization::Format<oxbox::serialization::BinaryFormat>);
static_assert(oxbox::serialization::Format<oxbox::serialization::PositionalFormat>);

static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::JsonFormat::Writer<oxbox::serialization::StringSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::JsonFormat::Writer<oxbox::serialization::OstreamSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::YamlFormat::Writer<oxbox::serialization::StringSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::YamlFormat::Writer<oxbox::serialization::OstreamSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::XmlFormat::Writer<oxbox::serialization::StringSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::XmlFormat::Writer<oxbox::serialization::OstreamSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::XmlPrettyFormat::Writer<oxbox::serialization::StringSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::XmlPrettyFormat::Writer<oxbox::serialization::OstreamSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::BinaryFormat::Writer<oxbox::serialization::StringSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::PositionalFormat::Writer<oxbox::serialization::StringSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::BinaryFormat::Writer<oxbox::serialization::OstreamSink>>);
static_assert(oxbox::serialization::WriterBackend<
  oxbox::serialization::PositionalFormat::Writer<oxbox::serialization::OstreamSink>>);

static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::JsonFormat::Reader<oxbox::serialization::StringSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::JsonFormat::Reader<oxbox::serialization::IstreamSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::YamlFormat::Reader<oxbox::serialization::StringSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::YamlFormat::Reader<oxbox::serialization::IstreamSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::XmlFormat::Reader<oxbox::serialization::StringSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::XmlFormat::Reader<oxbox::serialization::IstreamSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::XmlPrettyFormat::Reader<oxbox::serialization::StringSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::XmlPrettyFormat::Reader<oxbox::serialization::IstreamSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::BinaryFormat::Reader<oxbox::serialization::StringSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::PositionalFormat::Reader<oxbox::serialization::StringSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::BinaryFormat::Reader<oxbox::serialization::IstreamSource>>);
static_assert(oxbox::serialization::ReaderBackend<
  oxbox::serialization::PositionalFormat::Reader<oxbox::serialization::IstreamSource>>);


namespace
{

enum class Mode { QUIET, VERBOSE };

struct Probe {
  friend constexpr auto reflect_scheme(Probe*);

  std::string                  label;
  std::int32_t                 count{};
  bool                         on{};
  std::optional<std::string>   note;
  std::vector<std::string>     tags;
  Mode                         mode = Mode::QUIET;

  auto operator==(Probe const&) const -> bool = default;
};

constexpr auto reflect_scheme(Probe*)
{
  using T = Probe;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"label", &T::label>,
    ::reflect::member_scheme<"count", &T::count>,
    ::reflect::member_scheme<"on",    &T::on>,
    ::reflect::member_scheme<"note",  &T::note>,
    ::reflect::member_scheme<"tags",  &T::tags>,
    ::reflect::member_scheme<"mode",  &T::mode>>{ };
}

constexpr auto reflect_scheme(Mode*)
{
  using E = Mode;
  return ::reflect::enum_scheme<
    ::reflect::enumerator<"QUIET",   E::QUIET,   "", "quiet">,
    ::reflect::enumerator<"VERBOSE", E::VERBOSE, "", "verbose">>{ };
}

template <typename F>
class FormatContract : public ::testing::Test {};

using AllFormats = ::testing::Types<
  oxbox::serialization::JsonFormat,
  oxbox::serialization::YamlFormat,
  oxbox::serialization::XmlFormat,
  oxbox::serialization::XmlPrettyFormat,
  oxbox::serialization::BinaryFormat,
  oxbox::serialization::PositionalFormat>;

TYPED_TEST_SUITE(FormatContract, AllFormats);

template <typename F>
class NamedFormatContract : public ::testing::Test {};

using NamedFormats = ::testing::Types<
  oxbox::serialization::JsonFormat,
  oxbox::serialization::YamlFormat,
  oxbox::serialization::XmlFormat,
  oxbox::serialization::XmlPrettyFormat,
  oxbox::serialization::BinaryFormat>;

TYPED_TEST_SUITE(NamedFormatContract, NamedFormats);

template <typename F>
class BinaryFormatEnums : public ::testing::Test {};

using BinaryFormats = ::testing::Types<
  oxbox::serialization::BinaryFormat,
  oxbox::serialization::PositionalFormat>;

TYPED_TEST_SUITE(BinaryFormatEnums, BinaryFormats);


// Text corruption requires mapped enum names and editable text, neither of which the tagged binary wire provides.
template <typename F>
class TextFormatContract : public ::testing::Test {};

using TextFormats = ::testing::Types<
  oxbox::serialization::JsonFormat,
  oxbox::serialization::YamlFormat,
  oxbox::serialization::XmlFormat,
  oxbox::serialization::XmlPrettyFormat>;

TYPED_TEST_SUITE(TextFormatContract, TextFormats);

TYPED_TEST(FormatContract, RoundTripsFullProbe) {
  using F = TypeParam;
  Probe const orig{
    .label = "héllo 🌍 你好",
    .count = -7,
    .on    = true,
    .note  = "with note",
    .tags  = {"emoji-🚀", "rtl-עברית", "cjk-中文"},
    .mode  = Mode::VERBOSE,
  };
  auto const wire = oxbox::serialization::Serialize<F>(orig);
  auto const back = oxbox::serialization::Deserialize<F, Probe>(wire);
  EXPECT_EQ(back, orig);
}

TYPED_TEST(FormatContract, OptionalNullSurvives) {
  using F = TypeParam;
  Probe const orig{.label = "x", .count = 0, .on = false /* note=nullopt */};
  auto const wire = oxbox::serialization::Serialize<F>(orig);
  auto const back = oxbox::serialization::Deserialize<F, Probe>(wire);
  EXPECT_FALSE(back.note.has_value());
}

TYPED_TEST(FormatContract, EmptySequenceRoundTrips) {
  using F = TypeParam;
  Probe const orig{.label = "x", .count = 0, .on = false /* tags empty */};
  auto const wire = oxbox::serialization::Serialize<F>(orig);
  auto const back = oxbox::serialization::Deserialize<F, Probe>(wire);
  EXPECT_TRUE(back.tags.empty());
}

TYPED_TEST(FormatContract, EnumRoundTrips) {
  using F = TypeParam;
  for (auto m : {Mode::QUIET, Mode::VERBOSE}) {
    Probe const orig{.label = "x", .count = 0, .on = false, .mode = m};
    auto const wire = oxbox::serialization::Serialize<F>(orig);
    auto const back = oxbox::serialization::Deserialize<F, Probe>(wire);
    EXPECT_EQ(back.mode, m);
  }
}

TYPED_TEST(TextFormatContract, UnknownEnumValueThrowsParseError) {
  using F = TypeParam;
  Probe orig{.label = "x", .count = 0, .on = false, .mode = Mode::QUIET};
  auto wire = oxbox::serialization::Serialize<F>(orig);
  auto const at = wire.find("quiet");
  ASSERT_NE(at, std::string::npos) << wire;
  wire.replace(at, 5, "bogus");

  EXPECT_THROW(
    (oxbox::serialization::Deserialize<F, Probe>(wire)),
    oxbox::serialization::ParseError);
}

TYPED_TEST(TextFormatContract, TypeMismatchThrows) {
  using F = TypeParam;
  Probe const orig{.label = "x", .count = 42, .on = false};
  auto wire = oxbox::serialization::Serialize<F>(orig);
  auto const at = wire.find("42");
  ASSERT_NE(at, std::string::npos) << wire;
  wire.replace(at, 2, "\"forty-two\"");

  EXPECT_THROW(
    (oxbox::serialization::Deserialize<F, Probe>(wire)),
    oxbox::serialization::TypeMismatch);
}

TYPED_TEST(TextFormatContract, UnmappedEnumValueThrowsInvalidArgument) {
  using F = TypeParam;
  Probe const orig{ .label = "x", .count = 0, .on = false, .mode = Mode{ 99 } };
  EXPECT_THROW(
    (oxbox::serialization::Serialize<F>(orig)),
    oxbox::serialization::InvalidArgument);
}

// Binary enums carry integers, so absent wire-name mappings cannot cause unknown-name or unmapped-name errors.
TYPED_TEST(BinaryFormatEnums, ValueOutsideItsMapWritesAndReadsAsItsInteger) {
  using F = TypeParam;
  Probe const orig{ .label = "x", .count = 0, .on = false, .mode = Mode{ 99 } };
  auto const wire = oxbox::serialization::Serialize<F>(orig);
  EXPECT_EQ((oxbox::serialization::Deserialize<F, Probe>(wire).mode), Mode{ 99 });
}

TYPED_TEST(FormatContract, EveryEntryPointShape) {
  using F = TypeParam;
  Probe const orig{.label = "α", .count = 1, .on = true};

  oxbox::serialization::StringSink sink;
  oxbox::serialization::Serialize<F>(orig, sink);
  EXPECT_FALSE(sink.Out().empty());
  oxbox::serialization::StringSource src{sink.Out()};
  auto const back0 = oxbox::serialization::Deserialize<F, Probe>(src);
  EXPECT_EQ(back0, orig);

  auto const wire = oxbox::serialization::Serialize<F>(orig);
  auto const back1 = oxbox::serialization::Deserialize<F, Probe>(wire);
  EXPECT_EQ(back1, orig);

  std::ostringstream out;
  oxbox::serialization::SerializeTo<F>(orig, out);
  std::istringstream in{out.str()};
  auto const back2 = oxbox::serialization::DeserializeFrom<Probe>(in, F{});
  EXPECT_EQ(back2, orig);
}



struct MapProbe
{
  friend constexpr auto reflect_scheme(MapProbe*);

  std::string                                       name;
  std::map<std::string, std::int32_t>               ordered_map;
  std::unordered_map<std::string, std::string>      hash_map;

  auto operator==(MapProbe const&) const -> bool = default;
};

constexpr auto reflect_scheme(MapProbe*)
{
  using T = MapProbe;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"name",        &T::name>,
    ::reflect::member_scheme<"ordered_map", &T::ordered_map>,
    ::reflect::member_scheme<"hash_map",    &T::hash_map>>{ };
}

struct NestedInner
{
  friend constexpr auto reflect_scheme(NestedInner*);
  std::int32_t                        n{};
  std::map<std::string, std::string>  tags;
  auto operator==(NestedInner const&) const -> bool = default;
};

constexpr auto reflect_scheme(NestedInner*)
{
  using T = NestedInner;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"n",    &T::n>,
    ::reflect::member_scheme<"tags", &T::tags>>{ };
}

struct NestedOuter
{
  friend constexpr auto reflect_scheme(NestedOuter*);
  std::map<std::string, NestedInner> entries;
  auto operator==(NestedOuter const&) const -> bool = default;
};

constexpr auto reflect_scheme(NestedOuter*)
{
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"entries", &NestedOuter::entries>>{ };
}

TYPED_TEST(NamedFormatContract, PopulatedMapsRoundTrip)
{
  using F = TypeParam;
  MapProbe const orig{
    .name        = "thing",
    .ordered_map = {{"hits", 42}, {"misses", 7}},
    .hash_map    = {{"env", "prod"}, {"region", "eu-west-1"}},
  };
  auto const wire = oxbox::serialization::Serialize<F>(orig);
  auto const back = oxbox::serialization::Deserialize<F, MapProbe>(wire);
  EXPECT_EQ(back, orig);
}

TYPED_TEST(FormatContract, EmptyMapsRoundTrip)
{
  using F = TypeParam;
  MapProbe const orig{.name = "empty"};
  auto const wire = oxbox::serialization::Serialize<F>(orig);
  auto const back = oxbox::serialization::Deserialize<F, MapProbe>(wire);
  EXPECT_EQ(back, orig);
}

TYPED_TEST(NamedFormatContract, NestedMapValuesRoundTrip)
{
  using F = TypeParam;
  NestedOuter const orig{
    .entries = {
      {"a", NestedInner{.n = 1, .tags = {{"colour", "red"}}}},
      {"b", NestedInner{.n = 2, .tags = {{"colour", "blue"}, {"shape", "round"}}}},
    },
  };
  auto const wire = oxbox::serialization::Serialize<F>(orig);
  auto const back = oxbox::serialization::Deserialize<F, NestedOuter>(wire);
  EXPECT_EQ(back, orig);
}

struct TupleHolder
{
  std::tuple<std::int32_t, std::string, bool> triple{};
  bool operator==(TupleHolder const&) const = default;
};

constexpr auto reflect_scheme(TupleHolder*)
{
  using T = TupleHolder;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"triple", &T::triple>>{ };
}

TYPED_TEST(FormatContract, TupleFieldRoundTrips)
{
  using F = TypeParam;
  TupleHolder const orig{ .triple = { 42, "hi", true } };
  auto const wire = oxbox::serialization::Serialize<F>(orig);
  auto const back = oxbox::serialization::Deserialize<F, TupleHolder>(wire);
  EXPECT_EQ(back, orig);
}

}  // namespace
// NOLINTEND(misc-non-private-member-variables-in-classes)
