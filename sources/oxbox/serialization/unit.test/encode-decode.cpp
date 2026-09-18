// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests


// The _Encode/_Decode hook in both spellings -- members on the type, or a
// free pair found by ADL -- what it puts on the wire, how it composes inside
// a schemed struct, and where it sits relative to _Archive and _Restore.

#include "oxbox/serialization/io.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>


// ── Member form: Url encodes as a single string ─────────────────────
namespace test_encode_member {

struct Url {
  std::string scheme;
  std::string host;
  std::string path;

  auto _Encode() const -> std::string {
    return scheme + "://" + host + path;
  }

  static auto _Decode(std::string s) -> Url {
    auto const sep   = s.find("://");
    auto const rest  = s.substr(sep + 3);
    auto const slash = rest.find('/');
    return Url{
      .scheme = s.substr(0, sep),
      .host   = std::string{rest.substr(0, slash)},
      .path   = std::string{rest.substr(slash)},
    };
  }

  auto operator==(Url const&) const -> bool = default;
};

}

TEST(EncodeDecode, MemberFormRoundTrip) {
  test_encode_member::Url const orig{
    .scheme = "https",
    .host   = "example.com",
    .path   = "/api/v1",
  };
  auto const json = oxbox::serialization::ToJson(orig);

// 
  EXPECT_EQ(json, "\"https://example.com/api/v1\"");

  auto const back = oxbox::serialization::FromJson<test_encode_member::Url>(json);
  EXPECT_EQ(back, orig);
}

TEST(EncodeDecode, MemberFormWireShapeIsScalarNotObject) {
// 
  test_encode_member::Url const orig{
    .scheme = "http",
    .host   = "host",
    .path   = "/p",
  };
  EXPECT_EQ(oxbox::serialization::ToJson(orig), "\"http://host/p\"");
}


// ── ADL form: LegacyId encodes as an int ────────────────────────────
namespace test_encode_adl {

struct LegacyId {
  std::int32_t value{};
  auto operator==(LegacyId const&) const -> bool = default;
};

// In LegacyId's namespace — ADL discovers them.
inline auto _Encode(LegacyId const& id) -> std::int32_t {
  return id.value;
}
inline auto _Decode(std::type_identity<LegacyId>, std::int32_t v) -> LegacyId {
  return LegacyId{.value = v};
}

}

TEST(EncodeDecode, AdlFormRoundTrip) {
  test_encode_adl::LegacyId const orig{.value = 42};
  auto const json = oxbox::serialization::ToJson(orig);

  // Wire form is the bare int — no struct, no `value:` key.
  EXPECT_EQ(json, "42");

  auto const back = oxbox::serialization::FromJson<test_encode_adl::LegacyId>(json);
  EXPECT_EQ(back, orig);
}


// ── Composition: encoded fields inside a Schemed struct ─────────────
namespace test_encode_field {

struct Url {
  std::string scheme, host, path;
  auto _Encode() const -> std::string { return scheme + "://" + host + path; }
  static auto _Decode(std::string s) -> Url {
    auto const sep   = s.find("://");
    auto const rest  = s.substr(sep + 3);
    auto const slash = rest.find('/');
    return Url{s.substr(0, sep),
               std::string{rest.substr(0, slash)},
               std::string{rest.substr(slash)}};
  }
  auto operator==(Url const&) const -> bool = default;
};

struct Account {
  friend constexpr auto reflect_scheme(Account*);

  std::string  label;
  Url          home;

  auto operator==(Account const&) const -> bool = default;
};

constexpr auto reflect_scheme(Account*)
{
  using T = Account;
  return ::reflect::class_scheme<
  ::reflect::member_scheme<"label", &T::label>,
  ::reflect::member_scheme<"home", &T::home>>{ };
}

}

TEST(EncodeDecode, EncodedFieldsInsideStruct) {
// 
  test_encode_field::Account const orig{
    .label = "primary",
    .home  = test_encode_field::Url{
      .scheme = "https",
      .host   = "a.example.com",
      .path   = "/dashboard",
    },
  };
  auto const json = oxbox::serialization::ToJson(orig);

// 
  EXPECT_EQ(json,
    R"({"home":"https://a.example.com/dashboard","label":"primary"})");

  auto const back = oxbox::serialization::FromJson<test_encode_field::Account>(json);
  EXPECT_EQ(back, orig);
}


// ── Lifecycle ordering: _Archive → _Encode (write side) ─────────────
namespace test_archive_then_encode {

struct Tagged {
  std::string body;
  std::string trace;     // populated by _Archive, observed by _Encode

  void _Archive() { trace += "[archive]"; }

  auto _Encode() const -> std::string {
// 
    return body + trace;
  }

  static auto _Decode(std::string s) -> Tagged {
    return Tagged{.body = std::move(s), .trace = ""};
  }

  auto operator==(Tagged const&) const -> bool = default;
};

}

TEST(EncodeDecode, ArchiveRunsBeforeEncode) {
  test_archive_then_encode::Tagged orig{.body = "data"};
  auto const json = oxbox::serialization::ToJson(orig);

// 
  EXPECT_EQ(json, "\"data[archive]\"");
}


// ── Lifecycle ordering: _Decode → _Restore (read side) ──────────────
namespace test_decode_then_restore {

struct Computed {
  std::string raw;       // set by _Decode
  std::string normalized; // set by _Restore from raw

  static auto _Decode(std::string s) -> Computed {
    return Computed{.raw = std::move(s), .normalized = ""};
  }

  void _Restore() {
// 
    normalized = raw + "[restored]";
  }

  auto _Encode() const -> std::string { return raw; }

  auto operator==(Computed const&) const -> bool = default;
};

}

TEST(EncodeDecode, RestoreRunsAfterDecode) {
// 
  test_decode_then_restore::Computed const orig{
    .raw        = "payload",
    .normalized = "",        // discarded by _Encode (only `raw` goes on wire)
  };
  auto const json = oxbox::serialization::ToJson(orig);

  // Wire form pin: a single string from _Encode, not a struct.
  EXPECT_EQ(json, "\"payload\"");

  auto const back = oxbox::serialization::FromJson<test_decode_then_restore::Computed>(json);

  EXPECT_EQ(back.raw,        "payload");
  EXPECT_EQ(back.normalized, "payload[restored]")
    << "_Restore must run after _Decode populated `raw`";
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
