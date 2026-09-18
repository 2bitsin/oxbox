// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests

// The scheme tier end to end. The reflect_scheme definitions below are
// hand-written -- test-local types never reach the generator -- and are the
// shapes buildutil emits, except where a fixture deliberately says otherwise.

#include "oxbox/serialization/format-binary.hpp"
#include "oxbox/serialization/format-json.hpp"
#include "oxbox/serialization/format-node.hpp"
#include "oxbox/serialization/format-xml.hpp"
#include "oxbox/serialization/format-yaml.hpp"
#include "oxbox/serialization/formatter.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/query.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace
{
  namespace ser = oxbox::serialization;

  enum class Tone : std::uint8_t { SOFT, LOUD };
  constexpr auto reflect_scheme(Tone*);

  struct Inner
  {
    friend constexpr auto reflect_scheme(Inner*);

    std::int32_t depth{ 0 };                /* how far down */

    auto operator==(Inner const&) const -> bool = default;
  };

  struct Outer
  {
    friend constexpr auto reflect_scheme(Outer*);

    std::string  host{ };                   /* where to connect */
    std::int32_t port{ 0 };                 /* which port */
    Tone         tone{ Tone::SOFT };        /* how loud */
    Inner        inner{ };                  /* a reflected member */

    auto operator==(Outer const&) const -> bool = default;
  };

  // private state, reachable only because the tag is a friend
  class PrivateMembers
  {
  public:
    friend constexpr auto reflect_scheme(PrivateMembers*);

    PrivateMembers() = default;
    PrivateMembers(std::string secret, std::int32_t pin)
    : _secret{ std::move(secret) }
    , _pin   { pin }
    {}

    auto operator==(PrivateMembers const&) const -> bool = default;

  private:
    std::string  _secret{ };                /* never leaves the object */
    std::int32_t _pin{ 0 };                 /* four digits */
  };

  // a scheme is a list somebody wrote, not an inventory of the type
  struct Doubled
  {
    friend constexpr auto reflect_scheme(Doubled*);

    std::int32_t kept{ 0 };
    std::int32_t dropped{ 0 };
  };

  // enumerator labels: an enum reaching the wire under other names
  enum class Shade : std::uint8_t { PALE, DEEP };
  constexpr auto reflect_scheme(Shade*);

  struct Shaded
  {
    friend constexpr auto reflect_scheme(Shaded*);
    Shade shade{ Shade::PALE };
  };

  // never tagged: it must not borrow its base's reflection
  struct Untagged : Inner
  {
    std::int32_t extra{ 0 };
  };

  constexpr auto reflect_scheme(Tone*)
  {
    return ::reflect::enum_scheme<
      ::reflect::enumerator<"SOFT", Tone::SOFT, "say little">,
      ::reflect::enumerator<"LOUD", Tone::LOUD, "say a lot">>{ };
  }

  constexpr auto reflect_scheme(Shade*)
  {
    return ::reflect::enum_scheme<
      ::reflect::enumerator<"PALE", Shade::PALE, "", "pale">,
      ::reflect::enumerator<"DEEP", Shade::DEEP, "", "deep">>{ };
  }

  constexpr auto reflect_scheme(Inner*)
  {
    using T = Inner;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"depth", &T::depth, "how far down", false>>{ };
  }

  constexpr auto reflect_scheme(Outer*)
  {
    using T = Outer;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"host",  &T::host,  "where to connect",  false>,
      ::reflect::member_scheme<"port",  &T::port,  "which port",        false>,
      ::reflect::member_scheme<"tone",  &T::tone,  "how loud",          false>,
      ::reflect::member_scheme<"inner", &T::inner, "a reflected member", false>>{ };
  }

  constexpr auto reflect_scheme(PrivateMembers*)
  {
    using T = PrivateMembers;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_secret", &T::_secret, "never leaves the object", true>,
      ::reflect::member_scheme<"_pin",    &T::_pin,    "four digits",             true>>{ };
  }

  // `dropped` is deliberately absent and `kept` reaches the wire as "only"
  constexpr auto reflect_scheme(Doubled*)
  {
    using T = Doubled;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"kept", &T::kept, "", false, "only">>{ };
  }

  constexpr auto reflect_scheme(Shaded*)
  {
    using T = Shaded;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"shade", &T::shade, "", false>>{ };
  }

  static_assert(ser::HasScheme<Outer> && ser::HasReflectedScheme<Outer>);
  static_assert(ser::HasEnumMap<Tone> && ser::HasReflectedEnumMap<Tone>);

  // ADL on the base plus Untagged*-to-Inner* would otherwise answer here
  static_assert(!ser::HasScheme<Untagged>);

  // From here the shapes are `derived_scheme<base_list<...>, member...>`, what
  // the generator emits once a tagged type has a direct public non-virtual base.

  struct Vehicle
  {
    friend constexpr auto reflect_scheme(Vehicle*);

    std::int32_t wheels{ 0 };               /* how many touch the road */
    std::string  plate{ };                  /* as registered */

    auto operator==(Vehicle const&) const -> bool = default;
  };

  struct Car : Vehicle
  {
    friend constexpr auto reflect_scheme(Car*);

    std::int32_t seats{ 0 };                /* including the driver's */

    auto operator==(Car const&) const -> bool = default;
  };

  // three deep: an indirect base is never listed directly
  struct Root   { friend constexpr auto reflect_scheme(Root*);   std::int32_t r{ 0 }; };
  struct Middle : Root
  { friend constexpr auto reflect_scheme(Middle*); std::int32_t m{ 0 }; };
  struct Leaf : Middle
  { friend constexpr auto reflect_scheme(Leaf*);   std::int32_t l{ 0 }; };

  // no tag of its own: the scheme is written beside it, rename included
  struct Renamed { std::int32_t raw{ 0 }; };

  constexpr auto reflect_scheme(Renamed*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"raw", &Renamed::raw, "", false,
                               "renamed">>{ };
  }

  struct Reflected : Renamed
  {
    friend constexpr auto reflect_scheme(Reflected*);
    std::int32_t plain{ 0 };
  };

  // the cli's shape: a tag base with nothing in it, in every command's base list
  struct Marker { };

  struct Marked : Marker
  {
    friend constexpr auto reflect_scheme(Marked*);
    std::int32_t kept{ 0 };
  };

  // the price of that silence: a base with real fields is skipped as quietly
  struct Forgotten { std::int32_t lost{ 0 }; };

  struct Remembers : Forgotten
  {
    friend constexpr auto reflect_scheme(Remembers*);
    std::int32_t here{ 0 };
  };

  // a reference member's target has to outlive every object pointing at it
  Inner SHARED_HOOK{ .depth = 99 };   // NOLINT(*-avoid-non-const-global-variables)

  struct Hooked
  {
    friend constexpr auto reflect_scheme(Hooked*);

    std::int32_t count{ 0 };                /* ordinary data */
    Inner&       hook{ SHARED_HOOK };       /* a hook, never data */
    std::string  label{ };                  /* ordinary data */
  };

  // both name a field "shared": the derived member shadows the base's
  struct Trunk
  {
    friend constexpr auto reflect_scheme(Trunk*);
    std::int32_t shared{ 0 };
  };

  struct Branch : Trunk
  {
    friend constexpr auto reflect_scheme(Branch*);
    std::int32_t shared{ 0 };
    std::int32_t own{ 0 };
  };

  constexpr auto reflect_scheme(Vehicle*)
  {
    using T = Vehicle;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"wheels", &T::wheels, "how many touch the road", false>,
      ::reflect::member_scheme<"plate",  &T::plate,  "as registered",           false>>{ };
  }

  constexpr auto reflect_scheme(Car*)
  {
    using T = Car;
    return ::reflect::derived_scheme<
      ::reflect::base_list<Vehicle>,
      ::reflect::member_scheme<"seats", &T::seats, "including the driver's", false>>{ };
  }

  constexpr auto reflect_scheme(Root*)
  {
    using T = Root;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"r", &T::r, "", false>>{ };
  }

  constexpr auto reflect_scheme(Middle*)
  {
    using T = Middle;
    return ::reflect::derived_scheme<
      ::reflect::base_list<Root>,
      ::reflect::member_scheme<"m", &T::m, "", false>>{ };
  }

  constexpr auto reflect_scheme(Leaf*)
  {
    using T = Leaf;
    return ::reflect::derived_scheme<
      ::reflect::base_list<Middle>,
      ::reflect::member_scheme<"l", &T::l, "", false>>{ };
  }

  constexpr auto reflect_scheme(Reflected*)
  {
    using T = Reflected;
    return ::reflect::derived_scheme<
      ::reflect::base_list<Renamed>,
      ::reflect::member_scheme<"plain", &T::plain, "", false>>{ };
  }

  // Marker is not reflected; the language puts it in the base list anyway
  constexpr auto reflect_scheme(Marked*)
  {
    using T = Marked;
    return ::reflect::derived_scheme<
      ::reflect::base_list<Marker>,
      ::reflect::member_scheme<"kept", &T::kept, "", false>>{ };
  }

  constexpr auto reflect_scheme(Remembers*)
  {
    using T = Remembers;
    return ::reflect::derived_scheme<
      ::reflect::base_list<Forgotten>,
      ::reflect::member_scheme<"here", &T::here, "", false>>{ };
  }

  // the accessor-lambda form buildutil emits for a reference member: the
  // language has no pointer-to-member to a reference ([dcl.mptr]/3)
  constexpr auto reflect_scheme(Hooked*)
  {
    using T = Hooked;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"count", &T::count, "ordinary data", false>,
      ::reflect::member_scheme<"hook",
        [](auto&& owner) -> decltype(auto) { return (owner.hook); },
        "a hook, never data", false>,
      ::reflect::member_scheme<"label", &T::label, "ordinary data", false>>{ };
  }

  constexpr auto reflect_scheme(Trunk*)
  {
    using T = Trunk;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"shared", &T::shared, "", false>>{ };
  }

  constexpr auto reflect_scheme(Branch*)
  {
    using T = Branch;
    return ::reflect::derived_scheme<
      ::reflect::base_list<Trunk>,
      ::reflect::member_scheme<"shared", &T::shared, "", false>,
      ::reflect::member_scheme<"own",    &T::own,    "", false>>{ };
  }

  // a derived_scheme is a class scheme like any other, and Car's member list
  // names Car's own member
  static_assert(ser::HasReflectedScheme<Car>);
  static_assert(ser::HasScheme<Leaf> && ser::HasScheme<Marked>);

  // an accessor item in the member list must not break either guard
  static_assert(ser::HasReflectedScheme<Hooked>);

  // a scheme written beside a type describes that type, not what derives from it
  static_assert(ser::HasScheme<Renamed> && ser::HasScheme<Reflected>);

  auto const PROBE{ Outer{ .host = "héllo 🌍", .port = 8080,
                           .tone = Tone::LOUD, .inner = { .depth = 3 } } };
}


// The JSON backend emits object keys in sorted order, which is why the
// literals below are not in declaration order.
TEST(ReflectedScheme, TheFriendTagAloneMakesATypeSerializable) {
  EXPECT_EQ(ser::ToJson(PROBE),
            R"({"host":"héllo 🌍","inner":{"depth":3},"port":8080,"tone":"LOUD"})");
  EXPECT_EQ(ser::FromJson<Outer>(ser::ToJson(PROBE)), PROBE);
}

TEST(ReflectedScheme, AReflectedTypeRoundTripsInYaml) {
  EXPECT_EQ(ser::FromYaml<Outer>(ser::ToYaml(PROBE)), PROBE);
}

TEST(ReflectedScheme, AReflectedTypeRoundTripsInXml) {
  EXPECT_EQ(ser::FromXml<Outer>(ser::ToXml(PROBE)), PROBE);
}

TEST(ReflectedScheme, AReflectedTypeRoundTripsInBinary) {
  using F = ser::BinaryFormat;
  auto const bytes{ ser::SerializeBytes<F>(PROBE) };
  EXPECT_EQ((ser::DeserializeBytes<F, Outer>(bytes)), PROBE);
}

// the node format reads only: a Node is the parsed form
TEST(ReflectedScheme, AReflectedTypeReadsFromANodeTree) {
  ser::Node const tree{ ser::NodeObject{
    { "host",  ser::Node{ "héllo 🌍" } },
    { "port",  ser::Node{ "8080" } },
    { "tone",  ser::Node{ "LOUD" } },
    { "inner", ser::Node{ ser::NodeObject{ { "depth", ser::Node{ "3" } } } } },
  } };
  EXPECT_EQ(ser::FromNode<Outer>(tree), PROBE);
}

TEST(ReflectedScheme, AReflectedMemberOfAReflectedTypeIsWalkedTheSameWay) {
  auto const back{ ser::FromJson<Outer>(R"({"host":"h","port":1,"tone":"SOFT",)"
                                        R"("inner":{"depth":42}})") };
  EXPECT_EQ(back.inner.depth, 42);
}

TEST(ReflectedScheme, PrivateMembersAreSerializedState) {
  PrivateMembers const vault{ "s3cr3t", 4242 };
  EXPECT_EQ(ser::ToJson(vault), R"({"_pin":4242,"_secret":"s3cr3t"})");
  EXPECT_EQ(ser::FromJson<PrivateMembers>(ser::ToJson(vault)), vault);
}

TEST(ReflectedScheme, EnumeratorsSpellThemselvesVerbatim) {
  for (auto const tone : { Tone::SOFT, Tone::LOUD }) {
    auto const probe{ Outer{ .tone = tone } };
    EXPECT_EQ(ser::FromJson<Outer>(ser::ToJson(probe)).tone, tone);
  }
  EXPECT_NE(ser::ToJson(Outer{ .tone = Tone::LOUD }).find(R"("tone":"LOUD")"),
            std::string::npos);
}

TEST(ReflectedScheme, ASchemeSaysWhichMembersSerializeAndUnderWhatName) {
  EXPECT_EQ(ser::ToJson(Doubled{ .kept = 1, .dropped = 2 }), R"({"only":1})");
}

TEST(ReflectedScheme, AnEnumeratorLabelIsItsWireName) {
  EXPECT_EQ(ser::ToJson(Shaded{ .shade = Shade::DEEP }), R"({"shade":"deep"})");
  EXPECT_EQ(ser::FromJson<Shaded>(R"({"shade":"pale"})").shade, Shade::PALE);
}

TEST(ReflectedScheme, ADottedPathReachesIntoAReflectedType) {
  EXPECT_TRUE(ser::HasFieldNamed(PROBE, "inner.depth"));
  EXPECT_EQ(ser::GetField<std::int32_t>(PROBE, "inner.depth"), 3);

  Outer poked{ };
  EXPECT_TRUE(ser::SetField(poked, "inner.depth", std::int32_t{ 9 }));
  EXPECT_EQ(poked.inner.depth, 9);
  EXPECT_EQ(ser::FieldNames(Outer{})[3], "inner");
}

TEST(ReflectedScheme, TheDocCommentBecomesTheFieldDescription) {
  constexpr auto scheme{ ser::SchemeFor(Outer{}) };
  EXPECT_EQ(std::get<0>(scheme.fields).description, "where to connect");
  EXPECT_EQ(std::get<1>(scheme.fields).description, "which port");
  // reflection reports only the member's own spelling, so the override slot
  // stays empty and each format derives the wire name itself
  EXPECT_EQ(std::get<0>(scheme.fields).name, "host");
  EXPECT_TRUE(std::get<0>(scheme.fields).spelling.empty());
  EXPECT_EQ(std::get<0>(scheme.fields).WireName(), "host");
  // and the annotation never reaches the wire
  EXPECT_EQ(ser::ToJson(Outer{}),
            R"({"host":"","inner":{"depth":0},"port":0,"tone":"SOFT"})");
}

TEST(ReflectedScheme, AReflectedTypeFormatsThroughStdFormat) {
  EXPECT_EQ(std::format("{:json}", Outer{ .host = "h", .port = 2 }),
            R"({"host":"h","inner":{"depth":0},"port":2,"tone":"SOFT"})");
}


TEST(ReflectedBases, ADerivedTypeSerializesItsBasesFieldsThenItsOwn) {
  Car const car{ { .wheels = 4, .plate = "OX-1" }, 5 };

  EXPECT_EQ(ser::FieldNames(car),
            (std::array<std::string_view, 3>{ "wheels", "plate", "seats" }));

  auto const xml{ ser::ToXml(car) };
  EXPECT_LT(xml.find("<wheels>"), xml.find("<plate>")) << xml;
  EXPECT_LT(xml.find("<plate>"),  xml.find("<seats>")) << xml;
}

TEST(ReflectedBases, ADerivedTypeRoundTripsBaseFieldsAndItsOwn) {
  Car const car{ { .wheels = 4, .plate = "OX-1" }, 5 };
  // JSON sorts its keys, so this is plate, seats, wheels
  EXPECT_EQ(ser::ToJson(car), R"({"plate":"OX-1","seats":5,"wheels":4})");
  EXPECT_EQ(ser::FromJson<Car>(ser::ToJson(car)), car);
}

TEST(ReflectedBases, AGrandbaseArrivesThroughTheBaseThatListsIt) {
  Leaf in{ };
  in.r = 1; in.m = 2; in.l = 3;

  EXPECT_EQ(ser::FieldNames(in),
            (std::array<std::string_view, 3>{ "r", "m", "l" }));

  auto const back{ ser::FromJson<Leaf>(ser::ToJson(in)) };
  EXPECT_EQ(back.r, 1);
  EXPECT_EQ(back.m, 2);
  EXPECT_EQ(back.l, 3);
}

TEST(ReflectedBases, AHandWrittenBaseSchemeContributesToTheDerivedType) {
  // the base's rename comes through, and its fields come first
  Reflected in{ };
  in.raw = 7; in.plain = 8;
  EXPECT_EQ(ser::ToJson(in), R"({"plain":8,"renamed":7})");
  EXPECT_EQ(ser::FieldNames(in),
            (std::array<std::string_view, 2>{ "renamed", "plain" }));

  auto const back{ ser::FromJson<Reflected>(ser::ToJson(in)) };
  EXPECT_EQ(back.raw,   7);
  EXPECT_EQ(back.plain, 8);
}

TEST(ReflectedBases, AnUntaggedEmptyBaseIsSkippedInSilence) {
  // the cli's Command tag base in miniature: listed, unreflected, silent
  Marked in{ };
  in.kept = 3;
  EXPECT_EQ(ser::ToJson(in), R"({"kept":3})");
  EXPECT_EQ(ser::FromJson<Marked>(R"({"kept":3})").kept, 3);
}

TEST(ReflectedBases, AnUntaggedBaseWithFieldsIsSkippedJustAsQuietly) {
  // no diagnostic, and there cannot be one: the tier cannot tell a base
  // somebody forgot to tag from a tag base that never had fields
  Remembers in{ };
  in.lost = 1; in.here = 2;
  EXPECT_EQ(ser::ToJson(in), R"({"here":2})");
  EXPECT_EQ(ser::FieldNames(in),
            (std::array<std::string_view, 1>{ "here" }));
}


TEST(ReflectedScheme, AReferenceMemberIsNotSerializedState) {
  Hooked in{ };
  in.count = 4;
  in.label = "tag";

  // the hook is absent from the scheme entirely: not null, not empty, absent
  EXPECT_EQ(ser::FieldNames(in),
            (std::array<std::string_view, 2>{ "count", "label" }));
  EXPECT_EQ(ser::ToJson(in), R"({"count":4,"label":"tag"})");
  EXPECT_FALSE(ser::HasFieldNamed(in, "hook"));
}

TEST(ReflectedScheme, AReferenceMemberSurvivesAReadUntouched) {
  SHARED_HOOK.depth = 99;
  auto const back{ ser::FromJson<Hooked>(R"({"count":6,"label":"in"})") };
  EXPECT_EQ(back.count,      6);
  EXPECT_EQ(back.label,      "in");
  EXPECT_EQ(back.hook.depth, 99) << "the wire never had anything to say here";
}


TEST(ReflectedBases, ACollidingFieldNameIsLastOutAndBothIn) {
  // a scheme is a flat list that may name the same wire field twice, and
  // neither walker deduplicates
  Branch in{ };
  static_cast<Trunk&>(in).shared = 1;   // the base's
  in.shared = 2;                        // the derived's, shadowing
  in.own    = 3;

  EXPECT_EQ(ser::FieldNames(in),
            (std::array<std::string_view, 3>{ "shared", "shared", "own" }));

  // both are written into one wire field, in scheme order, so the last one
  // written is the one that survives
  EXPECT_EQ(ser::ToJson(in), R"({"own":3,"shared":2})");

  // one wire value, two slots expecting it: both end up holding it
  auto const back{ ser::FromJson<Branch>(R"({"own":9,"shared":7})") };
  EXPECT_EQ(static_cast<Trunk const&>(back).shared, 7);
  EXPECT_EQ(back.shared, 7);
  EXPECT_EQ(back.own,    9);
}

// NOLINTEND(misc-non-private-member-variables-in-classes)
