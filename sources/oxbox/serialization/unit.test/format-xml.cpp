// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


#include "oxbox/serialization/format-xml.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
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

  std::string                name;
  Config                     primary;
  std::optional<Config>      fallback;
  std::vector<std::string>   tags;

  auto operator==(Service const&) const -> bool = default;
};

constexpr auto reflect_scheme(Service*)
{
  using T = Service;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"name",     &T::name>,
    ::reflect::member_scheme<"primary",  &T::primary>,
    ::reflect::member_scheme<"fallback", &T::fallback>,
    ::reflect::member_scheme<"tags",     &T::tags>>{ };
}


// ── Compact wire shape ───────────────────────────────────────────────

TEST(Xml, CompactRootObjectIsSingleLine)
{
  Config const c{ .host = "h", .port = 8080, .tls = true };
  auto const wire{ oxbox::serialization::ToXml(c) };

  // Compact mode emits a single line.
  EXPECT_EQ(wire.find('\n'), std::string::npos);
  EXPECT_TRUE(wire.starts_with("<root>")) << wire;
  EXPECT_TRUE(wire.ends_with("</root>")) << wire;
// 
  EXPECT_NE(wire.find("<host>h</host>"),         std::string::npos) << wire;
  EXPECT_NE(wire.find("<port>8080</port>"),      std::string::npos) << wire;
  EXPECT_NE(wire.find("<tls>true</tls>"),        std::string::npos) << wire;
}

TEST(Xml, NulloptOptionalFieldIsAbsent)
{
  Service const s{ .name = "x", .primary = {}, .fallback = std::nullopt };
  auto const wire{ oxbox::serialization::ToXml(s) };

  EXPECT_EQ(wire.find("<fallback"), std::string::npos) << wire;
}

TEST(Xml, ArrayItemsShareItemTag)
{
  Service const s{ .name = "x", .primary = {}, .tags = { "a", "b" } };
  auto const wire{ oxbox::serialization::ToXml(s) };

  EXPECT_NE(wire.find("<tags>"),         std::string::npos) << wire;
  EXPECT_NE(wire.find("<item>a</item>"), std::string::npos) << wire;
  EXPECT_NE(wire.find("<item>b</item>"), std::string::npos) << wire;
}


// ── Pretty wire shape ────────────────────────────────────────────────

TEST(Xml, PrettyIndentsAcrossMultipleLines)
{
  Config const c{ .host = "h", .port = 8080, .tls = true };
  auto const wire{ oxbox::serialization::Serialize<oxbox::serialization::XmlPrettyFormat>(c) };

  // Pretty mode emits one element per line plus indentation.
  EXPECT_NE(wire.find('\n'), std::string::npos) << wire;
  EXPECT_NE(wire.find("  <host"), std::string::npos) << wire;
}

TEST(Xml, PrettyIsSameSemanticsAsCompact)
{
  // Both formats round-trip through the same Reader → same value.
  Config const orig{ .host = "h", .port = 7, .tls = false };
  auto const compact{ oxbox::serialization::ToXml(orig) };
  auto const pretty {
    oxbox::serialization::Serialize<oxbox::serialization::XmlPrettyFormat>(orig) };
  auto const from_compact{
    oxbox::serialization::FromXml<Config>(compact) };
  auto const from_pretty{
    oxbox::serialization::Deserialize<oxbox::serialization::XmlPrettyFormat, Config>(pretty) };
  EXPECT_EQ(from_compact, orig);
  EXPECT_EQ(from_pretty,  orig);
}


// ── Read errors ──────────────────────────────────────────────────────

TEST(Xml, MissingRootElementThrowsParseError)
{
  EXPECT_THROW(
    (oxbox::serialization::FromXml<Config>("<other/>")),
    oxbox::serialization::ParseError);
}

TEST(Xml, MalformedXmlThrowsParseError)
{
  EXPECT_THROW(
    (oxbox::serialization::FromXml<Config>("<root")),
    oxbox::serialization::ParseError);
}

struct Wide {
  friend constexpr auto reflect_scheme(Wide*);

  std::uint64_t big{};

  auto operator==(Wide const&) const -> bool = default;
};

constexpr auto reflect_scheme(Wide*)
{
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"big", &Wide::big>>{ };
}

TEST(Xml, SerializesUnsignedSixtyFourBit)
{
  Wide const w{ .big = 18446744073709551615ull };  // UINT64_MAX drives the u64 writer overload
  auto const wire{ oxbox::serialization::ToXml(w) };
  EXPECT_NE(wire.find("<big>18446744073709551615</big>"), std::string::npos) << wire;
  EXPECT_EQ(oxbox::serialization::FromXml<Wide>(wire), w);
}

}  // namespace
// NOLINTEND(misc-non-private-member-variables-in-classes)
