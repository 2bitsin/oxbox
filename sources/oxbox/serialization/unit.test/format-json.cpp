// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Config {
  friend constexpr auto reflect_scheme(Config*);

  std::string  host;
  std::int32_t port{};
  bool         tls{};

  auto operator==(Config const&) const -> bool = default;
};

constexpr auto reflect_scheme(Config*)
{
  using T = Config;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"host", &T::host>,
    ::reflect::member_scheme<"port", &T::port>,
    ::reflect::member_scheme<"tls",  &T::tls>>{ };
}

struct Service {
  friend constexpr auto reflect_scheme(Service*);

  std::string                                   name;
  Config                                        primary;
  std::optional<Config>                         fallback;
  std::vector<std::string>                      tags;
  std::unordered_map<std::string, std::int32_t> thresholds;

  auto operator==(Service const&) const -> bool = default;
};

constexpr auto reflect_scheme(Service*)
{
  using T = Service;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"name",       &T::name>,
    ::reflect::member_scheme<"primary",    &T::primary>,
    ::reflect::member_scheme<"fallback",   &T::fallback>,
    ::reflect::member_scheme<"tags",       &T::tags>,
    ::reflect::member_scheme<"thresholds", &T::thresholds>>{ };
}

// The wire spellings are not the enumerators' own, so each carries a LABEL.
// In a header the generator reads, a label is written at the declaration --
// `_Label(trace) TRACE` -- and the generator puts it in the scheme. A
// test-local enum never reaches the generator, so what it WOULD have
// emitted is written out here instead: the fourth argument.
enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR };

constexpr auto reflect_scheme(LogLevel*)
{
  using E = LogLevel;
  return ::reflect::enum_scheme<
    ::reflect::enumerator<"TRACE", E::TRACE, "", "trace">,
    ::reflect::enumerator<"DEBUG", E::DEBUG, "", "debug">,
    ::reflect::enumerator<"INFO",  E::INFO,  "", "info">,
    ::reflect::enumerator<"WARN",  E::WARN,  "", "warn">,
    ::reflect::enumerator<"ERROR", E::ERROR, "", "error">>{ };
}

struct Endpoint {
  friend constexpr auto reflect_scheme(Endpoint*);

  std::string host;
  LogLevel    level{LogLevel::INFO};

  auto operator==(Endpoint const&) const -> bool = default;
};

constexpr auto reflect_scheme(Endpoint*)
{
  using T = Endpoint;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"host",  &T::host>,
    ::reflect::member_scheme<"level", &T::level>>{ };
}

// A pointer-linked recursive shape, to exercise smart-pointer serialisation (write side).
struct Node {
  friend constexpr auto reflect_scheme(Node*);

  int                   value{};
  std::unique_ptr<Node> next;
};

constexpr auto reflect_scheme(Node*)
{
  using T = Node;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"value", &T::value>,
    ::reflect::member_scheme<"next",  &T::next>>{ };
}



TEST(Json, FullShape) {
  Service s{
    .name       = "x",
    .primary    = {.host = "h", .port = 1, .tls = false},
    .fallback   = std::nullopt,
    .tags       = {},
    .thresholds = {},
  };
  EXPECT_EQ(
    oxbox::serialization::ToJson(s),
    R"({"name":"x",)"
    R"("primary":{"host":"h","port":1,"tls":false},)"
    R"("tags":[],"thresholds":{}})");
}

TEST(Json, SmartPointerSerialisesAsPointee) {
  Node head{ .value = 1, .next = std::make_unique<Node>(Node{ .value = 2, .next = nullptr }) };
  EXPECT_EQ(oxbox::serialization::ToJson(head),
            R"({"next":{"next":null,"value":2},"value":1})");
}

TEST(Json, EnumExactShape) {
  Endpoint e{.host = "x", .level = LogLevel::WARN};
  EXPECT_EQ(
    oxbox::serialization::ToJson(e),
    R"({"host":"x","level":"warn"})");
}


// ── JSON-specific input parsing ──────────────────────────────────────
TEST(Json, MalformedInputThrowsParseError) {
  EXPECT_THROW(
    oxbox::serialization::FromJson<Config>("{not json"),
    oxbox::serialization::ParseError);
}

TEST(Json, EnumGivenAsNumberThrowsTypeMismatch) {
// 
  EXPECT_THROW(
    oxbox::serialization::FromJson<Endpoint>(R"({"host":"x","level":42})"),
    oxbox::serialization::TypeMismatch);
}

// ── octets ───────────────────────────────────────────────────────────
// A reader shows its values, so a run of octets is an array of integers
// there, not the binary wire's one length-prefixed node.
TEST(Json, OctetsAreAnArrayOfIntegers) {
  namespace ser = oxbox::serialization;
  std::vector<std::byte> const octets{ std::byte{ 0 }, std::byte{ 0x7f },
                                       std::byte{ 0xff } };
  auto const json{ ser::Serialize<ser::JsonFormat>(octets) };
  EXPECT_EQ(json, "[0,127,255]");
  EXPECT_EQ((ser::Deserialize<ser::JsonFormat, std::vector<std::byte>>(json)),
            octets);
  EXPECT_EQ((ser::Deserialize<ser::JsonFormat, std::array<std::byte, 3>>(
               json)), (std::array{ std::byte{ 0 }, std::byte{ 0x7f },
                                    std::byte{ 0xff } }));
}

TEST(Json, AnOctetOutOfRangeIsNamedWhereItStands) {
  namespace ser = oxbox::serialization;
  try {
    static_cast<void>(
      ser::Deserialize<ser::JsonFormat, std::vector<std::byte>>("[1,256]"));
    FAIL() << "256 does not fit an octet";
  } catch (ser::ParseError const& bad) {
    EXPECT_NE(std::string_view{ bad.what() }.find("256"),
              std::string_view::npos) << bad.what();
  }
}

}  // namespace

namespace oxbox::serialization::unit_test::detail::format_json
{
  struct GridCell {
    friend constexpr auto reflect_scheme(GridCell*) -> auto;

    std::int32_t column{};
    std::int32_t row{};

    auto operator<=>(GridCell const&) const -> std::strong_ordering = default;
  };

  constexpr auto reflect_scheme(GridCell*) -> auto
  {
    using T = GridCell;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"column", &T::column>,
      ::reflect::member_scheme<"row",    &T::row>>{ };
  }

  struct Tag {
    std::string text;

    auto operator<=>(Tag const&) const    -> std::strong_ordering = default;
    auto _Encode() const                  -> std::string { return text; }
    static auto _Decode(std::string wire) -> Tag      { return Tag{ std::move(wire) }; }
  };

  struct Slot {
    std::uint16_t number{};

    auto operator<=>(Slot const&) const     -> std::strong_ordering = default;
    auto _Encode() const                    -> std::uint16_t { return number; }
    static auto _Decode(std::uint16_t wire) -> Slot          { return Slot{ wire }; }
  };

  // both a string and a string-encoded key: the plain string path wins
  struct BothKey {
    std::string text;

    BothKey() = default;
    explicit BothKey(std::string_view name) : text{ name } {}
    operator std::string_view() const noexcept { return text; }

    auto operator<=>(BothKey const&) const -> std::strong_ordering = default;
    auto _Encode() const                   -> std::string { return "encoded:" + text; }
    static auto _Decode(std::string wire)  -> BothKey     { return BothKey{ std::string_view{ wire }.substr(8) }; }
  };
}

namespace ser = oxbox::serialization;

using ser::unit_test::detail::format_json::BothKey;
using ser::unit_test::detail::format_json::GridCell;
using ser::unit_test::detail::format_json::Slot;
using ser::unit_test::detail::format_json::Tag;

static_assert( ser::StringKeyed    <std::map<std::string,   std::int32_t>>);
static_assert(!ser::WireStringKeyed<std::map<std::string,   std::int32_t>>);
static_assert( ser::WireStringKeyed<std::map<Tag,           std::int32_t>>);
static_assert(!ser::StringKeyed    <std::map<Tag,           std::int32_t>>);
static_assert( ser::StringKeyed    <std::map<BothKey,       std::int32_t>>);
static_assert(!ser::WireStringKeyed<std::map<BothKey,       std::int32_t>>);
static_assert(!ser::ObjectKeyed    <std::map<std::uint16_t, std::int32_t>>);
static_assert(!ser::ObjectKeyed    <std::map<LogLevel,      std::int32_t>>);
static_assert(!ser::ObjectKeyed    <std::map<GridCell,      std::int32_t>>);
static_assert(!ser::ObjectKeyed    <std::map<Slot,          std::int32_t>>);
static_assert( ser::TupleLike      <std::pair<std::int32_t, std::string>>);
static_assert( ser::TupleLike      <std::tuple<>>);
static_assert(!ser::TupleLike      <std::array<std::int32_t, 2>>);

TEST(Json, StringKeyedMapsAreObjects) {
  EXPECT_EQ((ser::Serialize<ser::JsonFormat>(std::map<std::string, std::int32_t>{ { "a", 1 } })), R"({"a":1})");
  EXPECT_EQ((ser::Serialize<ser::JsonFormat>(std::map<Tag, std::int32_t>{ { Tag{ "b" }, 2 } })),  R"({"b":2})");
}

TEST(Json, AKeyThatIsBothStringAndEncodedTakesTheStringPath) {
  std::map<BothKey, std::int32_t> const orig{ { BothKey{ "a" }, 1 } };
  auto const json{ ser::Serialize<ser::JsonFormat>(orig) };
  EXPECT_EQ(json, R"({"a":1})");
  EXPECT_EQ((ser::Deserialize<ser::JsonFormat, std::map<BothKey, std::int32_t>>(json)), orig);
}

TEST(Json, IntegerKeyedMapIsAnArrayOfPairs) {
  std::map<std::uint16_t, Config> const orig{ { 1, Config{ .host = "h", .port = 2, .tls = true } }, { 3, Config{ } } };
  auto const json{ ser::Serialize<ser::JsonFormat>(orig) };
  EXPECT_EQ(json, R"([[1,{"host":"h","port":2,"tls":true}],[3,{"host":"","port":0,"tls":false}]])");
  EXPECT_EQ((ser::Deserialize<ser::JsonFormat, std::map<std::uint16_t, Config>>(json)), orig);
}

TEST(Json, EnumKeyedMapRoundTripsByName) {
  std::map<LogLevel, std::int32_t> const orig{ { LogLevel::ERROR, 1 }, { LogLevel::WARN, 2 } };
  auto const json{ ser::Serialize<ser::JsonFormat>(orig) };
  EXPECT_EQ(json, R"([["warn",2],["error",1]])");
  EXPECT_EQ((ser::Deserialize<ser::JsonFormat, std::map<LogLevel, std::int32_t>>(json)), orig);
}

TEST(Json, ReflectedStructKeyedMapRoundTrips) {
  std::map<GridCell, std::string> const orig{ { GridCell{ .column = 1, .row = 2 }, "ab" } };
  auto const json{ ser::Serialize<ser::JsonFormat>(orig) };
  EXPECT_EQ(json, R"([[{"column":1,"row":2},"ab"]])");
  EXPECT_EQ((ser::Deserialize<ser::JsonFormat, std::map<GridCell, std::string>>(json)), orig);
}

TEST(Json, EncodedKeyWithANumberWireIsAnArrayOfPairs) {
  std::map<Slot, bool> const orig{ { Slot{ 5 }, true } };
  auto const json{ ser::Serialize<ser::JsonFormat>(orig) };
  EXPECT_EQ(json, R"([[5,true]])");
  EXPECT_EQ((ser::Deserialize<ser::JsonFormat, std::map<Slot, bool>>(json)), orig);
}

// NOLINTEND(misc-non-private-member-variables-in-classes)
