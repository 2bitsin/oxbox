// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


// An inheriting type says `derived_scheme<base_list<Base>, member...>` and
// the tier concatenates the base's fields ahead of its own. A member
// pointer in a scheme must name a member of the type the scheme is FOR:
// `&Derived::inherited` is a pointer-to-member of the base and would make
// the scheme somebody else's, so a base's fields arrive through the
// base_list and no other way.
//
// Every reflect_scheme below is written out by hand, because a test-local
// type in a .cpp never reaches the generator -- the shapes are the ones
// buildutil emits.

#include "oxbox/serialization/io.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>


// ── Single-base inheritance ────────────────────────────────────────
namespace test_single_base
{
  struct Foo {
    friend constexpr auto reflect_scheme(Foo*);
    int         a = 0;
    std::string b;
  };

  constexpr auto reflect_scheme(Foo*)
  {
    using T = Foo;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"a", &T::a>,
    ::reflect::member_scheme<"b", &T::b>>{ };
  }


  struct Bar : Foo {
    friend constexpr auto reflect_scheme(Bar*);
    double c = 0.0;
  };

  constexpr auto reflect_scheme(Bar*)
  {
    using T = Bar;
    return ::reflect::derived_scheme<
    ::reflect::base_list<test_single_base::Foo>,
    ::reflect::member_scheme<"c", &T::c>>{ };
  }
}

TEST(Inheritance, SingleBaseRoundTrip) {
  test_single_base::Bar in;
  in.a = 7;
  in.b = "hi";
  in.c = 1.5;
  auto const json = oxbox::serialization::ToJson(in);
  auto       out  = oxbox::serialization::FromJson<test_single_base::Bar>(json);
  EXPECT_EQ(out.a, 7);
  EXPECT_EQ(out.b, "hi");
  EXPECT_EQ(out.c, 1.5);
}

TEST(Inheritance, SingleBaseAllFieldsOnWire) {
  test_single_base::Bar in;
  in.a = 1; in.b = "x"; in.c = 2.5;
// 
  auto const json = oxbox::serialization::ToJson(in);
  EXPECT_EQ(json, R"({"a":1,"b":"x","c":2.5})");
}


// ── Multi-base inheritance ─────────────────────────────────────────
namespace test_multi_base
{
  struct Left  { friend constexpr auto reflect_scheme(Left*);  int    l = 0; };
  struct Right { friend constexpr auto reflect_scheme(Right*); double r = 0; };

  constexpr auto reflect_scheme(Left*)
  {
    using T = Left;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"l", &T::l>>{ };
  }

  constexpr auto reflect_scheme(Right*)
  {
    using T = Right;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"r", &T::r>>{ };
  }

  struct Mixed : Left, Right {
    friend constexpr auto reflect_scheme(Mixed*);
    bool m = false;
  };

  constexpr auto reflect_scheme(Mixed*)
  {
    using T = Mixed;
    return ::reflect::derived_scheme<
    ::reflect::base_list<test_multi_base::Left, test_multi_base::Right>,
    ::reflect::member_scheme<"m", &T::m>>{ };
  }
}

TEST(Inheritance, MultiBaseRoundTrip) {
  test_multi_base::Mixed in;
  in.l = 11; in.r = 2.5; in.m = true;
  auto const json = oxbox::serialization::ToJson(in);
  auto       out  = oxbox::serialization::FromJson<test_multi_base::Mixed>(json);
  EXPECT_EQ(out.l, 11);
  EXPECT_EQ(out.r, 2.5);
  EXPECT_TRUE(out.m);
}

TEST(Inheritance, MultiBaseAllFieldsOnWire) {
  test_multi_base::Mixed in;
  in.l = 1; in.r = 0.5; in.m = false;
  // Alphabetical: l, m, r.
  EXPECT_EQ(oxbox::serialization::ToJson(in),
            R"({"l":1,"m":false,"r":0.5})");
}


// ── Three-deep chain ───────────────────────────────────────────────
namespace test_chain
{
  struct A { friend constexpr auto reflect_scheme(A*); int a = 0; };

  constexpr auto reflect_scheme(A*)
  {
    using T = A;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"a", &T::a>>{ };
  }

  struct B : A {
    friend constexpr auto reflect_scheme(B*);
    int b = 0;
  };

  constexpr auto reflect_scheme(B*)
  {
    using T = B;
    return ::reflect::derived_scheme<
    ::reflect::base_list<test_chain::A>,
    ::reflect::member_scheme<"b", &T::b>>{ };
  }

  struct C : B {
    friend constexpr auto reflect_scheme(C*);
    int c = 0;
  };

  constexpr auto reflect_scheme(C*)
  {
    using T = C;
    return ::reflect::derived_scheme<
    ::reflect::base_list<test_chain::B>,
    ::reflect::member_scheme<"c", &T::c>>{ };
  }
}

TEST(Inheritance, ThreeDeepChainRoundTrip) {
  test_chain::C in;
  in.a = 1; in.b = 2; in.c = 3;
  auto const json = oxbox::serialization::ToJson(in);
  EXPECT_EQ(json, R"({"a":1,"b":2,"c":3})");
  auto out = oxbox::serialization::FromJson<test_chain::C>(json);
  EXPECT_EQ(out.a, 1);
  EXPECT_EQ(out.b, 2);
  EXPECT_EQ(out.c, 3);
}


// ── Variant alternative inherits disc (the killer use case) ────────
namespace test_variant_inherit
{
  struct Disc {
    friend constexpr auto reflect_scheme(Disc*);
    std::string kind;
  };

  constexpr auto reflect_scheme(Disc*)
  {
    using T = Disc;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"kind", &T::kind>>{ };
  }

  struct Apple : Disc {
    friend constexpr auto reflect_scheme(Apple*);
    int weight = 0;
  };

  constexpr auto reflect_scheme(Apple*)
  {
    using T = Apple;
    return ::reflect::derived_scheme<
    ::reflect::base_list<test_variant_inherit::Disc>,
    ::reflect::member_scheme<"weight", &T::weight>>{ };
  }

  struct Banana : Disc {
    friend constexpr auto reflect_scheme(Banana*);
    std::string color;
  };

  constexpr auto reflect_scheme(Banana*)
  {
    using T = Banana;
    return ::reflect::derived_scheme<
    ::reflect::base_list<test_variant_inherit::Disc>,
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

TEST(Inheritance, VariantAlternativeInheritsDisc) {
// 
  test_variant_inherit::Apple a;
  a.kind = "apple";
  a.weight = 150;
  test_variant_inherit::Fruit in{a};
  auto const json = oxbox::serialization::ToJson(in);
  EXPECT_EQ(json, R"({"kind":"apple","weight":150})");
  auto out = oxbox::serialization::FromJson<test_variant_inherit::Fruit>(json);
  ASSERT_TRUE(std::holds_alternative<test_variant_inherit::Apple>(out));
  EXPECT_EQ(std::get<test_variant_inherit::Apple>(out).kind, "apple");
  EXPECT_EQ(std::get<test_variant_inherit::Apple>(out).weight, 150);
}

TEST(Inheritance, VariantInheritsDiscBananaArm) {
  test_variant_inherit::Banana b;
  b.kind = "banana";
  b.color = "yellow";
  test_variant_inherit::Fruit in{b};
  auto out = oxbox::serialization::FromJson<test_variant_inherit::Fruit>(
    oxbox::serialization::ToJson(in));
  ASSERT_TRUE(std::holds_alternative<test_variant_inherit::Banana>(out));
  EXPECT_EQ(std::get<test_variant_inherit::Banana>(out).color, "yellow");
}


// ── A hand-written scheme for a type you do not own, plus its base ──
// Neither type carries a friend tag: both schemes are written beside them,
// in their own namespace, where ADL finds them. Each is keyed on ITS type,
// so the child reaches the parent's field through its base_list.
namespace ext_parent
{
  struct ParentT { int p = 0; };

  constexpr auto reflect_scheme(ParentT*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"p", &ParentT::p>>{ };
  }

  struct ChildT : ParentT { int c = 0; };

  constexpr auto reflect_scheme(ChildT*)
  {
    return ::reflect::derived_scheme<
      ::reflect::base_list<ParentT>,
      ::reflect::member_scheme<"c", &ChildT::c>>{ };
  }
}

TEST(Inheritance, AHandWrittenSchemeReachesItsBasesFieldsThroughTheBaseList) {
  ext_parent::ChildT in;
  in.p = 4; in.c = 5;
  auto const json = oxbox::serialization::ToJson(in);
  // Alphabetical: c, p.
  EXPECT_EQ(json, R"({"c":5,"p":4})");
  auto out = oxbox::serialization::FromJson<ext_parent::ChildT>(json);
  EXPECT_EQ(out.p, 4);
  EXPECT_EQ(out.c, 5);
}


// ── Field-name conflict (laissez-faire policy) ─────────────────────
namespace test_conflict
{
  struct Base {
    friend constexpr auto reflect_scheme(Base*);
    int x = 0;
  };

  constexpr auto reflect_scheme(Base*)
  {
    using T = Base;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"x", &T::x>>{ };
  }

  // The collision is spelled the way REFLECTION can spell it: Child declares
  // its own `x`, SHADOWING the base's, so both schemes name a field called
  // "x". A hand-written scheme can produce the same collision a second way,
  // by naming an inherited member the child never declared, and reflection
  // cannot say that -- a generator reports the members a type declares. The
  // shadowing form is the honest reflected equivalent and produces the same
  // field list shape -- base "x", own "x", own "y" -- which is what this
  // test is here to pin.
  struct Child : Base {
    friend constexpr auto reflect_scheme(Child*);
    int x = 0;
    int y = 0;
  };

  constexpr auto reflect_scheme(Child*)
  {
    using T = Child;
    return ::reflect::derived_scheme<
    ::reflect::base_list<test_conflict::Base>,
    ::reflect::member_scheme<"x", &T::x>,
    ::reflect::member_scheme<"y", &T::y>>{ };
  }
}

TEST(Inheritance, FieldNameConflictLaissezFaire) {
  test_conflict::Child in;
  in.x = 9; in.y = 7;
  // Wire shape: `x` appears once (DOM dedup), then `y`.
  auto const json = oxbox::serialization::ToJson(in);
  EXPECT_EQ(json, R"({"x":9,"y":7})");
  auto out = oxbox::serialization::FromJson<test_conflict::Child>(json);
  EXPECT_EQ(out.x, 9);
  EXPECT_EQ(out.y, 7);
}


// ── Vector of derived ──────────────────────────────────────────────
TEST(Inheritance, VectorOfDerived) {
  std::vector<test_single_base::Bar> in(2);
  in[0].a = 1; in[0].b = "x"; in[0].c = 1.5;
  in[1].a = 2; in[1].b = "y"; in[1].c = 2.5;
  auto out = oxbox::serialization::FromJson<std::vector<test_single_base::Bar>>(
    oxbox::serialization::ToJson(in));
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].a, 1); EXPECT_EQ(out[0].b, "x"); EXPECT_EQ(out[0].c, 1.5);
  EXPECT_EQ(out[1].a, 2); EXPECT_EQ(out[1].b, "y"); EXPECT_EQ(out[1].c, 2.5);
}


// ── No-base call still works (backward compat) ─────────────────────
namespace test_no_base
{
  struct Plain {
    friend constexpr auto reflect_scheme(Plain*);
    int x = 0;
  };

  constexpr auto reflect_scheme(Plain*)
  {
    using T = Plain;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"x", &T::x>>{ };
  }
}

TEST(Inheritance, NoBaseStillWorks) {
  test_no_base::Plain in;
  in.x = 42;
  auto out = oxbox::serialization::FromJson<test_no_base::Plain>(
    oxbox::serialization::ToJson(in));
  EXPECT_EQ(out.x, 42);
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
