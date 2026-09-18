// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

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

// The wire spellings are not the enumerators' own, so each carries a LABEL.
// In a header the generator reads, a label is written at the declaration --
// `_Label(trace) TRACE` -- and the generator puts it in the scheme. A
// test-local enum never reaches the generator, so what it would have
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



TEST(Yaml, BlockStyleMappingExactShape) {
  Config c{.host = "api.example.com", .port = 8443, .tls = true};
  EXPECT_EQ(
    oxbox::serialization::ToYaml(c),
    "host: api.example.com\nport: 8443\ntls: true\n");
}

TEST(Yaml, EnumExactShape) {
  Endpoint e{.host = "x", .level = LogLevel::ERROR};
  EXPECT_EQ(
    oxbox::serialization::ToYaml(e),
    "host: x\nlevel: error\n");
}


// ── YAML-specific input parsing ──────────────────────────────────────
TEST(Yaml, MalformedInputThrowsParseError) {
  EXPECT_THROW(
    oxbox::serialization::FromYaml<Config>("\tnot: valid\n\tyaml: at all"),
    oxbox::serialization::ParseError);
}

}  // namespace
// NOLINTEND(misc-non-private-member-variables-in-classes)
