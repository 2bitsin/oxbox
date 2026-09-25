// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
// NOLINTEND(misc-non-private-member-variables-in-classes)
