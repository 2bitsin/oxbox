// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


// Discriminated variants: a `_Restore(V const&) -> V` beside the variant is
// the only dispatch, and it is re-asked until it stops changing the arm.
//
// One route to a scheme: an ADL `reflect_scheme(T*)`. Every reflect_scheme
// below is written out by hand, because a test-local type in a .cpp never
// reaches the generator -- the shapes are the ones buildutil emits.

#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/io.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>


// ── Fixture: simple two-arm discriminated variant ──────────────────
namespace test_disc
{
  struct Disc {
    friend constexpr auto reflect_scheme(Disc*);
    std::string kind;
  };

  struct Apple {
    friend constexpr auto reflect_scheme(Apple*);
    std::string kind;
    int         weight;
  };

  struct Banana {
    friend constexpr auto reflect_scheme(Banana*);
    std::string kind;
    std::string color;
  };

  constexpr auto reflect_scheme(Disc*)
  {
    using T = Disc;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>>{ };
  }

  constexpr auto reflect_scheme(Apple*)
  {
    using T = Apple;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>,
    ::reflect::member_scheme<"weight", &T::weight>>{ };
  }

  constexpr auto reflect_scheme(Banana*)
  {
    using T = Banana;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>,
    ::reflect::member_scheme<"color", &T::color>>{ };
  }

  using Fruit = std::variant<Disc, Apple, Banana>;

  inline auto _Restore(Fruit const& v) -> Fruit {
    if (auto const* d = std::get_if<Disc>(&v)) {
      if (d->kind == "apple")  return Apple{};
      if (d->kind == "banana") return Banana{};
    }
    return v;
  }
}

TEST(DiscriminatedVariant, RoundTripApple) {
  test_disc::Fruit f{test_disc::Apple{.kind = "apple", .weight = 150}};
  auto const json = oxbox::serialization::ToJson(f);
  auto       back = oxbox::serialization::FromJson<test_disc::Fruit>(json);
  ASSERT_TRUE(std::holds_alternative<test_disc::Apple>(back));
  EXPECT_EQ(std::get<test_disc::Apple>(back).kind,   "apple");
  EXPECT_EQ(std::get<test_disc::Apple>(back).weight, 150);
}

TEST(DiscriminatedVariant, RoundTripBanana) {
  test_disc::Fruit f{test_disc::Banana{.kind = "banana", .color = "yellow"}};
  auto const json = oxbox::serialization::ToJson(f);
  auto       back = oxbox::serialization::FromJson<test_disc::Fruit>(json);
  ASSERT_TRUE(std::holds_alternative<test_disc::Banana>(back));
  EXPECT_EQ(std::get<test_disc::Banana>(back).color, "yellow");
}

TEST(DiscriminatedVariant, UnknownDiscriminatorStaysAsDisc) {
  auto back = oxbox::serialization::FromJson<test_disc::Fruit>(
    R"({"kind":"carrot"})");
  ASSERT_TRUE(std::holds_alternative<test_disc::Disc>(back));
  EXPECT_EQ(std::get<test_disc::Disc>(back).kind, "carrot");
}

TEST(DiscriminatedVariant, DefaultConstructedRoundTrip) {
// 
  test_disc::Fruit f;
  auto const json = oxbox::serialization::ToJson(f);
  auto       back = oxbox::serialization::FromJson<test_disc::Fruit>(json);
  ASSERT_TRUE(std::holds_alternative<test_disc::Disc>(back));
}


// ── Fixture: multi-level dispatch (A → B → C) ──────────────────────
namespace test_multilevel
{
  struct GroupDisc {
    friend constexpr auto reflect_scheme(GroupDisc*);
    std::string kind;
  };

  struct AnimalDisc {
    friend constexpr auto reflect_scheme(AnimalDisc*);
    std::string kind;
    std::string species;
  };

  struct Cat {
    friend constexpr auto reflect_scheme(Cat*);
    std::string kind;
    std::string species;
    int         lives = 9;
  };

  constexpr auto reflect_scheme(GroupDisc*)
  {
    using T = GroupDisc;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>>{ };
  }

  constexpr auto reflect_scheme(AnimalDisc*)
  {
    using T = AnimalDisc;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>,
    ::reflect::member_scheme<"species", &T::species>>{ };
  }

  constexpr auto reflect_scheme(Cat*)
  {
    using T = Cat;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>,
    ::reflect::member_scheme<"species", &T::species>,
    ::reflect::member_scheme<"lives", &T::lives>>{ };
  }

  using Thing = std::variant<GroupDisc, AnimalDisc, Cat>;

  inline auto _Restore(Thing const& v) -> Thing {
    if (auto const* g = std::get_if<GroupDisc>(&v)) {
      if (g->kind == "animal") return AnimalDisc{};
    }
    if (auto const* a = std::get_if<AnimalDisc>(&v)) {
      if (a->species == "cat") return Cat{};
    }
    return v;
  }
}

TEST(DiscriminatedVariant, MultiLevelDispatch) {
// 
  auto back = oxbox::serialization::FromJson<test_multilevel::Thing>(
    R"({"kind":"animal","species":"cat","lives":7})");
  ASSERT_TRUE(std::holds_alternative<test_multilevel::Cat>(back));
  EXPECT_EQ(std::get<test_multilevel::Cat>(back).lives, 7);
}

TEST(DiscriminatedVariant, MultiLevelStopsMidChain) {
  // species="dog" doesn't match Cat → AnimalDisc stays.
  auto back = oxbox::serialization::FromJson<test_multilevel::Thing>(
    R"({"kind":"animal","species":"dog"})");
  ASSERT_TRUE(std::holds_alternative<test_multilevel::AnimalDisc>(back));
  EXPECT_EQ(std::get<test_multilevel::AnimalDisc>(back).species, "dog");
}


// ── Fixture: cyclic _Restore (A → B → A) ───────────────────────────
namespace test_cycle
{
  struct Disc { friend constexpr auto reflect_scheme(Disc*); std::string kind; };
  struct A    { friend constexpr auto reflect_scheme(A*);    std::string kind; };
  struct B    { friend constexpr auto reflect_scheme(B*);    std::string kind; };

  constexpr auto reflect_scheme(Disc*)
  {
    using T = Disc;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>>{ };
  }

  constexpr auto reflect_scheme(A*)
  {
    using T = A;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>>{ };
  }

  constexpr auto reflect_scheme(B*)
  {
    using T = B;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>>{ };
  }

  using V = std::variant<Disc, A, B>;

  inline auto _Restore(V const& v) -> V {
    if (std::holds_alternative<Disc>(v)) return A{};
    if (std::holds_alternative<A>(v))    return B{};
    if (std::holds_alternative<B>(v))    return A{};   // cycle: would revisit A
    return v;
  }
}

TEST(DiscriminatedVariant, CycleThrows) {
  EXPECT_THROW(
    oxbox::serialization::FromJson<test_cycle::V>(R"({"kind":"x"})"),
    oxbox::serialization::ParseError);
}


// ── Variant inside a containing scheme ─────────────────────────────
namespace test_nested
{
  struct Box {
    friend constexpr auto reflect_scheme(Box*);
    std::string      label;
    test_disc::Fruit content;
  };

  constexpr auto reflect_scheme(Box*)
  {
    using T = Box;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"label", &T::label>,
    ::reflect::member_scheme<"content", &T::content>>{ };
  }
}

TEST(DiscriminatedVariant, NestedInScheme) {
  test_nested::Box const b{
    .label   = "crate-1",
    .content = test_disc::Apple{.kind = "apple", .weight = 200},
  };
  auto const json = oxbox::serialization::ToJson(b);
  auto       back = oxbox::serialization::FromJson<test_nested::Box>(json);
  EXPECT_EQ(back.label, "crate-1");
  ASSERT_TRUE(std::holds_alternative<test_disc::Apple>(back.content));
  EXPECT_EQ(std::get<test_disc::Apple>(back.content).weight, 200);
}

TEST(DiscriminatedVariant, VectorOfVariants) {
  std::vector<test_disc::Fruit> const fruits = {
    test_disc::Apple {.kind = "apple",  .weight = 100},
    test_disc::Banana{.kind = "banana", .color  = "ripe"},
    test_disc::Apple {.kind = "apple",  .weight = 200},
  };
  auto const json = oxbox::serialization::ToJson(fruits);
  auto       back = oxbox::serialization::FromJson<std::vector<test_disc::Fruit>>(json);
  ASSERT_EQ(back.size(), 3u);
  ASSERT_TRUE(std::holds_alternative<test_disc::Apple> (back[0]));
  ASSERT_TRUE(std::holds_alternative<test_disc::Banana>(back[1]));
  ASSERT_TRUE(std::holds_alternative<test_disc::Apple> (back[2]));
  EXPECT_EQ(std::get<test_disc::Apple> (back[0]).weight, 100);
  EXPECT_EQ(std::get<test_disc::Banana>(back[1]).color, "ripe");
  EXPECT_EQ(std::get<test_disc::Apple> (back[2]).weight, 200);
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
