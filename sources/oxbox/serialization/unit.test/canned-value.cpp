// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests
#include "oxbox/serialization/canned-value.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/format-json.hpp"
#include "oxbox/serialization/format-yaml.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

namespace {

using oxbox::serialization::Canned;
using oxbox::serialization::Uncan;



struct OuterEnvelope {
  friend constexpr auto reflect_scheme(OuterEnvelope*);

  std::string name;
  Canned      payload;
};

constexpr auto reflect_scheme(OuterEnvelope*)
{
  using T = OuterEnvelope;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"name",    &T::name>,
    ::reflect::member_scheme<"payload", &T::payload>>{ };
}

// A concrete typed payload to Uncan into after the fact.
struct TypedPayload {
  friend constexpr auto reflect_scheme(TypedPayload*);

  std::int64_t count;
  std::string  label;

  auto operator==(TypedPayload const&) const -> bool = default;
};

constexpr auto reflect_scheme(TypedPayload*)
{
  using T = TypedPayload;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"count", &T::count>,
    ::reflect::member_scheme<"label", &T::label>>{ };
}


// ── Tests ────────────────────────────────────────────────────────────
TEST(Canned, YamlPayloadCapturesYamlNode)
{
  std::string const yaml = R"(
name: "envelope"
payload:
  count: 42
  label: "hello"
)";

  std::istringstream in{yaml};
  auto outer = oxbox::serialization::DeserializeFrom<OuterEnvelope>(
    in, oxbox::serialization::YamlFormat{});

  EXPECT_EQ(outer.name, "envelope");
  // YAML branch of the variant must hold.
  ASSERT_TRUE(std::holds_alternative<YAML::Node>(outer.payload));
  auto const& node = std::get<YAML::Node>(outer.payload);
  EXPECT_TRUE(node.IsMap());
}

TEST(Canned, JsonPayloadCapturesJsonValue)
{
  std::string const json = R"({
    "name": "envelope",
    "payload": { "count": 42, "label": "hello" }
  })";

  std::istringstream in{json};
  auto outer = oxbox::serialization::DeserializeFrom<OuterEnvelope>(
    in, oxbox::serialization::JsonFormat{});

  EXPECT_EQ(outer.name, "envelope");
  ASSERT_TRUE(std::holds_alternative<nlohmann::json>(outer.payload));
  auto const& node = std::get<nlohmann::json>(outer.payload);
  EXPECT_TRUE(node.is_object());
  EXPECT_EQ(node.at("count").get<int>(), 42);
}

TEST(Canned, UncanFromYamlMaterialisesTypedPayload)
{
  std::string const yaml = R"(
name: "envelope"
payload:
  count: 42
  label: "hello"
)";

  std::istringstream in{yaml};
  auto outer = oxbox::serialization::DeserializeFrom<OuterEnvelope>(
    in, oxbox::serialization::YamlFormat{});

  auto const typed = Uncan<TypedPayload>(outer.payload);
  EXPECT_EQ(typed, (TypedPayload{42, "hello"}));
}

TEST(Canned, UncanFromJsonMaterialisesTypedPayload)
{
  std::string const json = R"({
    "name": "envelope",
    "payload": { "count": 42, "label": "hello" }
  })";

  std::istringstream in{json};
  auto outer = oxbox::serialization::DeserializeFrom<OuterEnvelope>(
    in, oxbox::serialization::JsonFormat{});

  auto const typed = Uncan<TypedPayload>(outer.payload);
  EXPECT_EQ(typed, (TypedPayload{42, "hello"}));
}

TEST(Canned, UncanCrossFormatProducesSameValue)
{
// 
  std::string const yaml = R"(
name: "envelope"
payload: { count: 7, label: "k" }
)";
  std::string const json = R"({
    "name": "envelope",
    "payload": { "count": 7, "label": "k" }
  })";

  std::istringstream y{yaml};
  std::istringstream j{json};
  auto from_yaml = oxbox::serialization::DeserializeFrom<OuterEnvelope>(
    y, oxbox::serialization::YamlFormat{});
  auto from_json = oxbox::serialization::DeserializeFrom<OuterEnvelope>(
    j, oxbox::serialization::JsonFormat{});

  auto const yaml_payload = Uncan<TypedPayload>(from_yaml.payload);
  auto const json_payload = Uncan<TypedPayload>(from_json.payload);
  EXPECT_EQ(yaml_payload, json_payload);
  EXPECT_EQ(yaml_payload, (TypedPayload{7, "k"}));
}

TEST(Canned, UncanScalarFromYaml)
{
// 
  std::string const yaml = R"(
name: "scalar-env"
payload: 99
)";

  std::istringstream in{yaml};
  auto outer = oxbox::serialization::DeserializeFrom<OuterEnvelope>(
    in, oxbox::serialization::YamlFormat{});

  EXPECT_EQ(Uncan<std::int64_t>(outer.payload), 99);
}

TEST(Canned, UncanTypeMismatchThrows)
{
// 
  std::string const yaml = R"(
name: "mismatch-env"
payload: "not-a-typed-payload"
)";

  std::istringstream in{yaml};
  auto outer = oxbox::serialization::DeserializeFrom<OuterEnvelope>(
    in, oxbox::serialization::YamlFormat{});

  EXPECT_THROW(
    (void)Uncan<TypedPayload>(outer.payload),
    oxbox::serialization::TypeMismatch);
}

}  // namespace
// NOLINTEND(misc-non-private-member-variables-in-classes)
