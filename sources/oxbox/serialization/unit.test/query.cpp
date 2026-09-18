// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests
#include "oxbox/serialization/query.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

namespace {

struct Inner {
  friend constexpr auto reflect_scheme(Inner*);
  std::int32_t a{};
  std::int32_t b{};
  auto operator==(Inner const&) const -> bool = default;
};

constexpr auto reflect_scheme(Inner*)
{
  using T = Inner;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"a", &T::a>,
    ::reflect::member_scheme<"b", &T::b>>{ };
}

struct Registers {
  friend constexpr auto reflect_scheme(Registers*);
  std::uint32_t eax{};
  std::uint16_t cs{};
  bool          zero{};
  std::string   tag{};
  double        scale{};
  Inner         nested{};
};

constexpr auto reflect_scheme(Registers*)
{
  using T = Registers;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"eax",    &T::eax>,
    ::reflect::member_scheme<"cs",     &T::cs>,
    ::reflect::member_scheme<"zero",   &T::zero>,
    ::reflect::member_scheme<"tag",    &T::tag>,
    ::reflect::member_scheme<"scale",  &T::scale>,
    ::reflect::member_scheme<"nested", &T::nested>>{ };
}

using oxbox::serialization::JsonFormat;
using oxbox::serialization::FieldForm;

// enumeration folds at compile time -- prove it, don't just run it
static_assert(oxbox::serialization::detail::SchemeFieldCount<Registers> == 6u);
static_assert(oxbox::serialization::FieldNames(Registers{})[0] == "eax");
static_assert(oxbox::serialization::FieldNames(Registers{})[5] == "nested");
static_assert(oxbox::serialization::Fields(Registers{})[0].Form() == FieldForm::Unsigned);
static_assert(oxbox::serialization::Fields(Registers{})[2].Form() == FieldForm::Boolean);
static_assert(oxbox::serialization::Fields(Registers{})[3].Form() == FieldForm::Text);
static_assert(oxbox::serialization::Fields(Registers{})[4].Form() == FieldForm::Real);
static_assert(oxbox::serialization::Fields(Registers{})[5].Form() == FieldForm::Aggregate);

TEST(Query, HasFieldNamed) {
  Registers regs{};
  EXPECT_TRUE(oxbox::serialization::HasFieldNamed(regs, "cs"));
  EXPECT_FALSE(oxbox::serialization::HasFieldNamed(regs, "nope"));
}

TEST(Query, GetFieldTyped) {
  Registers regs{};
  regs.eax = 0xDEADBEEFu;
  auto value = oxbox::serialization::GetField<std::uint32_t>(regs, "eax");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 0xDEADBEEFu);
  EXPECT_FALSE(oxbox::serialization::GetField<std::uint32_t>(regs, "nope").has_value());
}

TEST(Query, GetFieldWrongTypeIsEmpty) {
  Registers regs{};
  regs.tag = "hello";
  EXPECT_FALSE(oxbox::serialization::GetField<std::uint32_t>(regs, "tag").has_value());
}

TEST(Query, SetFieldTyped) {
  Registers regs{};
  EXPECT_TRUE(oxbox::serialization::SetField<std::uint32_t>(regs, "eax", 0x1234u));
  EXPECT_EQ(regs.eax, 0x1234u);
  EXPECT_FALSE(oxbox::serialization::SetField<std::uint32_t>(regs, "nope", 1u));
}

TEST(Query, SerializeOneFieldToJson) {
  Registers regs{};
  regs.eax = 255u;
  auto json = oxbox::serialization::SerializeField<JsonFormat>(regs, "eax");
  ASSERT_TRUE(json.has_value());
  EXPECT_EQ(*json, "255");
  EXPECT_FALSE(oxbox::serialization::SerializeField<JsonFormat>(regs, "nope").has_value());
}

TEST(Query, SerializeNestedFieldToJson) {
  Registers regs{};
  regs.nested = Inner{ .a = 1, .b = 2 };
  auto json = oxbox::serialization::SerializeField<JsonFormat>(regs, "nested");
  ASSERT_TRUE(json.has_value());
  EXPECT_NE(json->find("\"a\""), std::string::npos);
  EXPECT_NE(json->find("\"b\""), std::string::npos);
}

TEST(Query, DeserializeOneFieldFromJson) {
  Registers regs{};
  EXPECT_TRUE(oxbox::serialization::DeserializeField<JsonFormat>(regs, "cs", "4660"));  // 0x1234
  EXPECT_EQ(regs.cs, 0x1234u);
  EXPECT_FALSE(oxbox::serialization::DeserializeField<JsonFormat>(regs, "nope", "1"));
}

TEST(Query, DeserializeLeavesOtherFieldsUntouched) {
  Registers regs{};
  regs.eax = 7u;
  oxbox::serialization::DeserializeField<JsonFormat>(regs, "cs", "9");
  EXPECT_EQ(regs.eax, 7u);
  EXPECT_EQ(regs.cs, 9u);
}

// --- state reached through accessors, named on the wire by its labels ---
// The scheme names the members; the accessors are how the rest of the
// program touches them, and the labels are what the wire sees.
struct Accessored {
  std::uint32_t _raw{};
  std::uint16_t _tag{};

  constexpr auto GetRaw() const -> std::uint32_t { return _raw; }
  constexpr auto SetRaw(std::uint32_t value) -> void { _raw = value; }
  constexpr auto GetTag() const -> std::uint16_t { return _tag; }
  constexpr auto SetTag(std::uint16_t value) -> void { _tag = value; }

  auto operator==(Accessored const&) const -> bool = default;
};

constexpr auto reflect_scheme(Accessored*)
{
  using T = Accessored;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"_raw", &T::_raw, "", false, "raw">,
    ::reflect::member_scheme<"_tag", &T::_tag, "", false, "tag">>{ };
}

static_assert(oxbox::serialization::detail::SchemeFieldCount<Accessored> == 2u);
static_assert(oxbox::serialization::FieldNames(Accessored{})[0] == "raw");
static_assert(oxbox::serialization::Fields(Accessored{})[0].Form() == FieldForm::Unsigned);

TEST(QueryLabelled, GetAndSetByLabel) {
  Accessored acc{};
  EXPECT_TRUE(oxbox::serialization::SetField<std::uint32_t>(acc, "raw", 0xABCDu));
  EXPECT_EQ(acc.GetRaw(), 0xABCDu);
  auto value = oxbox::serialization::GetField<std::uint32_t>(acc, "raw");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 0xABCDu);
}

TEST(QueryLabelled, SerializeOneFieldByLabel) {
  Accessored acc{};
  acc.SetRaw(255u);
  auto json = oxbox::serialization::SerializeField<JsonFormat>(acc, "raw");
  ASSERT_TRUE(json.has_value());
  EXPECT_EQ(*json, "255");
}

TEST(QueryLabelled, DeserializeOneFieldByLabel) {
  Accessored acc{};
  EXPECT_TRUE(oxbox::serialization::DeserializeField<JsonFormat>(acc, "tag", "9"));
  EXPECT_EQ(acc.GetTag(), 9u);
}

TEST(QueryLabelled, WholeObjectRoundTrips) {
  Accessored acc{};
  acc.SetRaw(0xDEADBEEFu);
  acc.SetTag(0x1234u);
  auto back = oxbox::serialization::FromJson<Accessored>(oxbox::serialization::ToJson(acc));
  EXPECT_EQ(back, acc);
}

// --- labelled and unlabelled fields in one scheme ---
struct Mixed {
  std::string   label{};
  std::uint32_t _count{};

  constexpr auto GetCount() const -> std::uint32_t { return _count; }
  constexpr auto SetCount(std::uint32_t value) -> void { _count = value; }

  auto operator==(Mixed const&) const -> bool = default;
};

constexpr auto reflect_scheme(Mixed*)
{
  using T = Mixed;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"label",  &T::label>,
    ::reflect::member_scheme<"_count", &T::_count, "", false, "count">>{ };
}

TEST(QueryLabelled, LabelledAndUnlabelledFieldsRoundTrip) {
  Mixed mixed{};
  mixed.label = "hi";
  mixed.SetCount(7u);
  auto back = oxbox::serialization::FromJson<Mixed>(oxbox::serialization::ToJson(mixed));
  EXPECT_EQ(back, mixed);
  auto count = oxbox::serialization::GetField<std::uint32_t>(back, "count");
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 7u);
}

// --- dotted-path descent into nested schemes (the whole-machine query shape) ---
struct Outer {
  friend constexpr auto reflect_scheme(Outer*);
  Registers  regs{};                     // member-field sub-scheme
  Accessored acc{};                      // labelled sub-scheme
};

constexpr auto reflect_scheme(Outer*)
{
  using T = Outer;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"regs", &T::regs>,
    ::reflect::member_scheme<"acc",  &T::acc>>{ };
}

TEST(QueryPath, ReadNestedFieldByPath) {
  Outer outer{};
  outer.regs.eax = 0xCAFEu;
  outer.acc.SetRaw(0x99u);
  auto eax = oxbox::serialization::GetField<std::uint32_t>(outer, "regs.eax");
  ASSERT_TRUE(eax.has_value());
  EXPECT_EQ(*eax, 0xCAFEu);
  auto raw = oxbox::serialization::GetField<std::uint32_t>(outer, "acc.raw");
  ASSERT_TRUE(raw.has_value());
  EXPECT_EQ(*raw, 0x99u);
}

TEST(QueryPath, HasFieldNamedFollowsPath) {
  Outer outer{};
  EXPECT_TRUE(oxbox::serialization::HasFieldNamed(outer, "regs.eax"));
  EXPECT_TRUE(oxbox::serialization::HasFieldNamed(outer, "acc.raw"));
  EXPECT_FALSE(oxbox::serialization::HasFieldNamed(outer, "regs.nope"));
  EXPECT_FALSE(oxbox::serialization::HasFieldNamed(outer, "nope.eax"));
  EXPECT_FALSE(oxbox::serialization::HasFieldNamed(outer, "regs.eax.deeper"));  // eax is scalar
}

TEST(QueryPath, SerializeNestedFieldByPath) {
  Outer outer{};
  outer.regs.cs = 0x1234u;
  auto json = oxbox::serialization::SerializeField<JsonFormat>(outer, "regs.cs");
  ASSERT_TRUE(json.has_value());
  EXPECT_EQ(*json, "4660");
}

TEST(QueryPath, PokeNestedFieldByPath) {
  Outer outer{};
  EXPECT_TRUE(oxbox::serialization::SetField<std::uint32_t>(outer, "regs.eax", 0xBEEFu));
  EXPECT_EQ(outer.regs.eax, 0xBEEFu);
  EXPECT_TRUE(oxbox::serialization::SetField<std::uint32_t>(outer, "acc.raw", 0x77u));
  EXPECT_EQ(outer.acc.GetRaw(), 0x77u);
  EXPECT_FALSE(oxbox::serialization::SetField<std::uint32_t>(outer, "regs.nope", 1u));
}

TEST(QueryPath, DeserializeNestedFieldByPath) {
  Outer outer{};
  EXPECT_TRUE(oxbox::serialization::DeserializeField<JsonFormat>(outer, "regs.cs", "4660"));
  EXPECT_EQ(outer.regs.cs, 0x1234u);
}

TEST(QueryPath, FieldNamesAtDescends) {
  Outer outer{};
  EXPECT_EQ(oxbox::serialization::FieldNamesAt(outer, "").size(), 2u);          // regs, acc
  auto const regs{ oxbox::serialization::FieldNamesAt(outer, "regs") };
  EXPECT_TRUE(std::ranges::any_of(regs, [](auto const& name) { return name == "eax"; }));
  EXPECT_TRUE(oxbox::serialization::FieldNamesAt(outer, "regs.eax").empty());   // scalar leaf
  EXPECT_TRUE(oxbox::serialization::FieldNamesAt(outer, "nope").empty());       // unresolved
}

// --- format-free scalar marshaling (the non-janky pybind path) ---
TEST(QueryScalar, ReadLeafAsScalar) {
  Registers regs{};
  regs.eax = 0xDEADu;
  regs.zero = true;
  regs.tag = "hi";
  regs.scale = 2.5;
  EXPECT_EQ(std::get<std::uint64_t>(oxbox::serialization::GetScalar(regs, "eax")), 0xDEADu);
  EXPECT_EQ(std::get<bool>(oxbox::serialization::GetScalar(regs, "zero")), true);
  EXPECT_EQ(std::get<std::string>(oxbox::serialization::GetScalar(regs, "tag")), "hi");
  EXPECT_EQ(std::get<double>(oxbox::serialization::GetScalar(regs, "scale")), 2.5);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(oxbox::serialization::GetScalar(regs, "nested")));
  EXPECT_TRUE(std::holds_alternative<std::monostate>(oxbox::serialization::GetScalar(regs, "nope")));
}

TEST(QueryScalar, WriteLeafFromScalar) {
  Registers regs{};
  EXPECT_TRUE(oxbox::serialization::SetScalar(regs, "eax", oxbox::serialization::FieldScalar{ std::uint64_t{ 0x1234 } }));
  EXPECT_EQ(regs.eax, 0x1234u);
  EXPECT_FALSE(oxbox::serialization::SetScalar(regs, "nope", oxbox::serialization::FieldScalar{ std::uint64_t{ 1 } }));
  EXPECT_FALSE(oxbox::serialization::SetScalar(regs, "eax", oxbox::serialization::FieldScalar{ }));   // monostate
}

TEST(QueryScalar, NestedPathScalar) {
  Outer outer{};
  outer.regs.eax = 7u;
  EXPECT_EQ(std::get<std::uint64_t>(oxbox::serialization::GetScalar(outer, "regs.eax")), 7u);
  EXPECT_TRUE(oxbox::serialization::SetScalar(outer, "acc.raw", oxbox::serialization::FieldScalar{ std::uint64_t{ 0x55 } }));
  EXPECT_EQ(outer.acc.GetRaw(), 0x55u);
}

}  // namespace
// NOLINTEND(misc-non-private-member-variables-in-classes)
