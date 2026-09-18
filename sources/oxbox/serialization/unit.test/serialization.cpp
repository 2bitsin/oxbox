// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


// Scheme dispatch and the generic Serialize/Deserialize entry point, plus
// tuples and encapsulated types. Discriminated variants, the _Encode/_Decode
// hook and inheritance have their own files beside this one.
//
// One route to a scheme: an ADL `reflect_scheme(T*)`. Every reflect_scheme
// below is written out by hand, because a test-local type in a .cpp never
// reaches the generator -- the shapes are the ones buildutil emits.

#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/format-positional.hpp"
#include "oxbox/serialization/io.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>


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
  ::reflect::member_scheme<"tls", &T::tls>>{ };
}

// A path member must reach the read walker's string-to-path conversion.
struct WithPath {
  friend constexpr auto reflect_scheme(WithPath*);

  std::filesystem::path where;
};

constexpr auto reflect_scheme(WithPath*)
{
  using T = WithPath;
  return ::reflect::class_scheme<
  ::reflect::member_scheme<"where", &T::where>>{ };
}

TEST(Serialize, PathMemberRoundTripsJson) {
  WithPath const original{ "directory with spaces/file.txt" };
  auto const back = oxbox::serialization::FromJson<WithPath>(
    oxbox::serialization::ToJson(original));
  EXPECT_EQ(back.where, original.where);
}

TEST(Serialize, PathMemberRoundTripsPositional) {
  using F = oxbox::serialization::PositionalFormat;
  WithPath const original{ "directory with spaces/file.txt" };
  auto const wire = oxbox::serialization::Serialize<F>(original);
  auto const back = oxbox::serialization::Deserialize<F, WithPath>(wire);
  EXPECT_EQ(back.where, original.where);
}

// A renamed field and two descriptions -- all three reachable through
// reflection: the description is the member's doc comment, the rename is
// its _Label. In a generated header this reads `_Label(secure) bool tls;`
// and nothing else; written out here because a test-local type never
// reaches the generator.
struct Annotated {
  friend constexpr auto reflect_scheme(Annotated*);

  std::string  host;
  std::int32_t port{};
  bool         tls{};
};

constexpr auto reflect_scheme(Annotated*)
{
  using T = Annotated;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"host", &T::host>,
    ::reflect::member_scheme<"port", &T::port, "listen port, 1..65535">,
    ::reflect::member_scheme<"tls",  &T::tls,  "require TLS", false,
                             "secure">>{ };
}

struct MaybeFields {
  friend constexpr auto reflect_scheme(MaybeFields*);

  std::optional<std::int32_t>   opt;
  std::unique_ptr<std::int32_t> ptr;
};

constexpr auto reflect_scheme(MaybeFields*)
{
  using T = MaybeFields;
  return ::reflect::class_scheme<
  ::reflect::member_scheme<"opt", &T::opt>,
  ::reflect::member_scheme<"ptr", &T::ptr>>{ };
}

TEST(Scheme, NulloptFieldIsAbsentEmptyPointerIsNull) {
  EXPECT_EQ(oxbox::serialization::ToJson(MaybeFields{}), R"({"ptr":null})");

  MaybeFields full;
  full.opt = 7;
  full.ptr = std::make_unique<std::int32_t>(9);
  EXPECT_EQ(oxbox::serialization::ToJson(full), R"({"opt":7,"ptr":9})");
}

TEST(Scheme, ReadersTolerateAbsentAndNullForOptionalsAndPointers) {
  for (auto const* wire : {R"({})", R"({"opt":null,"ptr":null})"}) {
    auto const got = oxbox::serialization::FromJson<MaybeFields>(wire);
    EXPECT_EQ(got.opt, std::nullopt) << wire;
    EXPECT_EQ(got.ptr, nullptr) << wire;
  }

  auto const got = oxbox::serialization::FromJson<MaybeFields>(R"({"opt":1,"ptr":2})");
  EXPECT_EQ(got.opt, 1);
  ASSERT_NE(got.ptr, nullptr);
  EXPECT_EQ(*got.ptr, 2);
}

TEST(Scheme, FieldDescriptionsAreMetadataNotWire) {
  constexpr auto scheme = oxbox::serialization::SchemeFor(Annotated{});
  EXPECT_EQ(std::get<0>(scheme.fields).description, "");
  EXPECT_EQ(std::get<1>(scheme.fields).description, "listen port, 1..65535");
  EXPECT_EQ(std::get<2>(scheme.fields).name, "tls");          // the member's own spelling
  EXPECT_EQ(std::get<2>(scheme.fields).spelling, "secure");    // the overridden one
  EXPECT_EQ(std::get<2>(scheme.fields).WireName(), "secure");
  EXPECT_EQ(std::get<2>(scheme.fields).description, "require TLS");
  // the annotation never reaches the wire
  EXPECT_EQ(oxbox::serialization::ToJson(Annotated{}), R"({"host":"","port":0,"secure":false})");
}

TEST(Scheme, MissingFieldThrows) {
// 
  EXPECT_THROW(
    oxbox::serialization::FromJson<Config>(R"({"host":"x"})"),
    oxbox::serialization::MissingField);
}

TEST(Scheme, TypeMismatchThrows) {
// 
  EXPECT_THROW(
    oxbox::serialization::FromJson<Config>(R"({"host":"x","port":"nope","tls":false})"),
    oxbox::serialization::TypeMismatch);
}

TEST(Serialize, ConstOverloadAppliesWhenNoArchiveHook) {
  Config const c{.host = "h", .port = 1, .tls = false};
  // Compiles — Config has no _Archive hook, so the const overload applies.
  auto json = oxbox::serialization::ToJson(c);
  EXPECT_FALSE(json.empty());
}

TEST(Serialize, GenericEntryPointIntoStringSink) {
  Config const c{.host = "x", .port = 1, .tls = true};
  oxbox::serialization::StringSink sink;
  oxbox::serialization::Serialize<oxbox::serialization::JsonFormat>(c, sink);
  EXPECT_FALSE(sink.Out().empty());

  oxbox::serialization::StringSource src{sink.Out()};
  auto back = oxbox::serialization::Deserialize<oxbox::serialization::JsonFormat, Config>(src);
  EXPECT_EQ(back, c);
}

}  // namespace
// NOLINTEND(misc-non-private-member-variables-in-classes)

// ── Short wire arrays into tuples ──
namespace test_tuple_tail
{
  struct Holder {
    friend constexpr auto reflect_scheme(Holder*);
    std::tuple<std::string, std::optional<std::int32_t>> t;
  };
  struct Pair {
    friend constexpr auto reflect_scheme(Pair*);
    std::tuple<std::string, std::int32_t> t;
  };

  constexpr auto reflect_scheme(Holder*)
  {
    using T = Holder;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"t", &T::t>>{ };
  }

  constexpr auto reflect_scheme(Pair*)
  {
    using T = Pair;
    return ::reflect::class_scheme<
    ::reflect::member_scheme<"t", &T::t>>{ };
  }
}

TEST(Tuple, ShortWireArrayFillsOptionalTailWithNullopt) {
  auto const got = oxbox::serialization::FromJson<test_tuple_tail::Holder>(R"({"t":["q"]})");
  EXPECT_EQ(std::get<0>(got.t), "q");
  EXPECT_EQ(std::get<1>(got.t), std::nullopt);
}

TEST(Tuple, ShortWireArrayMissingRequiredElementThrows) {
  EXPECT_THROW(
    (oxbox::serialization::FromJson<test_tuple_tail::Pair>(R"({"t":["q"]})")),
    oxbox::serialization::ParseError);
}

// ── encapsulated types: private/protected members ────────────────────

namespace test_encapsulated
{
  // Private state reflects, and the leading underscore that private state
  // carries in this codebase is exactly the sort of thing a LABEL is for:
  // `_secret` is the member, `secret` is what leaves the object. The friend
  // tag grants the visibility; the label supplies the external name.
  class PrivateMembers
  {
  public:
    friend constexpr auto reflect_scheme(PrivateMembers*);

    PrivateMembers() = default;
    PrivateMembers(std::string secret, std::int32_t pin)
    : _secret{ std::move(secret) }, _pin{ pin } {}

    auto operator==(PrivateMembers const&) const -> bool = default;

  private:
    std::string  _secret;
    std::int32_t _pin{ 0 };
  };

  constexpr auto reflect_scheme(PrivateMembers*)
  {
    using T = PrivateMembers;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_secret", &T::_secret, "", true, "secret">,
      ::reflect::member_scheme<"_pin",    &T::_pin,    "", true, "pin">>{ };
  }

  // Protected base state, reaching the wire through the derived type.
  // Reflection reports the members a type DECLARES, and the derived type
  // declares none of its own -- so the labels live on the base, which
  // carries its own tag, and the derived type picks them up through its
  // base list like any other reflected base.
  class ProtectedBase
  {
  public:
    friend constexpr auto reflect_scheme(ProtectedBase*);

    auto operator==(ProtectedBase const&) const -> bool = default;
    auto Fill(std::string id, std::int32_t rev) -> void
    { _id = std::move(id); _rev = rev; }

  protected:
    std::string  _id;
    std::int32_t _rev{ 0 };
  };

  constexpr auto reflect_scheme(ProtectedBase*)
  {
    using T = ProtectedBase;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_id",  &T::_id,  "", true, "id">,
      ::reflect::member_scheme<"_rev", &T::_rev, "", true, "rev">>{ };
  }

  class InheritsProtectedBase : public ProtectedBase
  {
  public:
    friend constexpr auto reflect_scheme(InheritsProtectedBase*);

    auto operator==(InheritsProtectedBase const&) const -> bool = default;
  };

  // a tagged type that declares nothing of its own: everything it
  // serializes arrives through the base list
  constexpr auto reflect_scheme(InheritsProtectedBase*)
  {
    return ::reflect::derived_scheme<
      ::reflect::base_list<test_encapsulated::ProtectedBase>>{ };
  }

  // one private member, and the label is the only thing that decides what
  // the wire calls it
  class LabelledPrivateMember
  {
  public:
    friend constexpr auto reflect_scheme(LabelledPrivateMember*);

    LabelledPrivateMember() = default;
    explicit LabelledPrivateMember(std::int32_t code) : _code{ code } {}
    auto operator==(LabelledPrivateMember const&) const -> bool = default;

  private:
    std::int32_t _code{ 0 };
  };

  constexpr auto reflect_scheme(LabelledPrivateMember*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_code", &LabelledPrivateMember::_code, "", true,
                               "code">>{ };
  }

  // NO SCHEME DECLARED IS NO SCHEME AT ALL. Encapsulating state says
  // nothing about serializing it: no reflect_scheme, no scheme
  // (compile-time pin).
  class NoRouteTaken
  {
  private:
    std::int32_t _x{ 0 };
  };
  static_assert(!oxbox::serialization::HasScheme<NoRouteTaken>);
  static_assert(oxbox::serialization::HasScheme<PrivateMembers>);
}  // namespace test_encapsulated

namespace enc = test_encapsulated;

TEST(Encapsulated, PrivateMembersRoundTrip) {
  enc::PrivateMembers const original{ "s3cr3t", 4242 };
  auto const round = oxbox::serialization::FromJson<enc::PrivateMembers>(
    oxbox::serialization::ToJson(original));
  EXPECT_EQ(round, original);
}

TEST(Encapsulated, ProtectedBaseMembersReachTheWireThroughTheirOwnTag) {
  enc::InheritsProtectedBase original;
  original.Fill("core", 7);
  auto const round = oxbox::serialization::FromJson<enc::InheritsProtectedBase>(
    oxbox::serialization::ToJson(original));
  EXPECT_EQ(round, original);
}

TEST(Encapsulated, ALabelIsTheWireNameOfAPrivateMember) {
  enc::LabelledPrivateMember const original{ 99 };
  // the member is `_code`; the wire says `code`, and only the label says so
  EXPECT_EQ(oxbox::serialization::ToJson(original), R"({"code":99})");
  auto const round = oxbox::serialization::FromJson<enc::LabelledPrivateMember>(
    oxbox::serialization::ToJson(original));
  EXPECT_EQ(round, original);
}
