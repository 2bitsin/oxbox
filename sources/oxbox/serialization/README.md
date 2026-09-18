# serialization

Round-trip user types against wire formats. Six formats are registered in
`AllFormats` (`format-pack.hpp`): `json`, `yaml`, `xml`, `xml-pretty`,
`binary` and `positional`, with `xml-pretty` as the pack default. The format
slot is pluggable. `NodeFormat` is a seventh Format, over an in-memory DOM, and is
deliberately not in the pack. Any serializable type is also
`std::format`-able: `std::format("{:json}", obj)`, empty spec = the pack
default.

## Quick start

```cpp
#include "oxbox/serialization/io.hpp"

using namespace oxbox;

struct Config {
  friend constexpr auto reflect_scheme(Config*);   // the whole opt-in

  std::string  host;
  std::int32_t port;
  bool         tls;
};

Config c{ };
auto json = serialization::ToJson(c);                    // string
auto yaml = serialization::ToYaml(c);                    // string
serialization::SerializeTo(c, "/tmp/config.yaml");       // extension picks the format

auto back = serialization::DeserializeFrom<Config>("/tmp/config.yaml");
```

## What's where

| File | Holds |
|------|-------|
| `concepts.hpp` | `Sink`, `Source` (the streaming byte contracts: advancing-span `Read`/`Write`, `docs/streaming-io.md` §2), `Character` / `StringSequence(Of)` / `SequenceCharT`, `Writer`, `Reader`, `Format`, `_OwnsEveryItem`, and the `Has*` detection concepts |
| `stream.hpp` | the contract plumbing: `detail::PutBytes`/`PutText` drain loops for writers, `detail::SourceBuf`/`SourceStream` (a `std::streambuf` over a `Source`, for DOM parsers), `detail::DrainBytes` (binary's blob pull) |
| `scheme.hpp` | the declarative vocabulary: `Field<T,M>` (name, pointer-to-member, description, `_Label` spelling), `Scheme<T, Fs...>`, `EnumMap<E,N>` and its deduction guides, and the `IsStdOptionalT` / `FixedSequence` traits the field machinery dispatches on |
| `hooks.hpp` | the per-type opt-in points: `IsCannedT`/`IsCanned` (the canned-subtree marker consumers specialize) and the `RunArchiveHook`/`RunRestoreHook` lifecycle calls |
| `reflected-scheme.hpp` | turns what `reflect_scheme(T*)` emits into a `Scheme` (own fields, bases in declaration order, reference members dropped) or an `EnumMap`, plus the tier accessors `SchemeFor`/`EnumMapFor` |
| `write-walker.hpp` | `detail::WriteWalker<W>` — walks a scheme against a writer backend, one `Visit` overload per supported shape. Absent-versus-null on the wire is settled here |
| `read-walker.hpp` | `detail::ReadWalker<R>` — the read half of the same contract. The only walker `canned-value.hpp`, `format-node.hpp` and the read side of `query.hpp` need |
| `serializable.hpp` | the module entry point: includes the five above and holds the primary `Serialize<F>(T, Sink&)` (both overloads) / `Deserialize<F, T>(Source&)` |
| `format-json.hpp` | `JsonFormat` (nlohmann::json backend) — `Writer<S>` builds a DOM tree and dumps once on `Flush`; `Reader<Src>` parses on construct and navigates by reference |
| `format-yaml.hpp` | `YamlFormat` (yaml-cpp backend) — `Writer<S>` streams events into a `YAML::Emitter`; `Reader<Src>` parses, then path-resolves from root on every access (yaml-cpp's `Node::operator=` mutates aliased data) |
| `format-xml.hpp` | `XmlFormat` and `XmlPrettyFormat` (pugixml backend) — one DOM-building `Writer` and one DOM-walking `Reader` between them; the two differ only in pugixml save flags (`format_raw` against `format_indent`) |
| `binary-wire.hpp` | shared tags, varints, byte spans and the bounds-checked `Cursor` |
| `binary-reader.hpp` / `binary-writer.hpp` | shared binary navigation and emission for named or positional objects |
| `format-binary.hpp` | `BinaryFormat` — a tagged binary wire with named fields |
| `format-positional.hpp` | `PositionalFormat` — the same primitives, with count-prefixed objects in scheme order and no field names |
| `node.hpp` / `format-node.hpp` | `Node`, an owning in-memory DOM (`variant<monostate, string, Array, Object>`), and `NodeFormat`, the Format over it — for building and inspecting values without committing to a wire |
| `format-pack.hpp` | `FormatPack<F...>` — the format registry: by-name and by-extension lookup into a `variant`, `Visit(name, fn)`. `AllFormats` is the declaration site; `DEFAULT_FORMAT_NAME` is `"xml-pretty"` |
| `format-traits.hpp` | `FormatTraits<F>` per pack format — name, extension claims, record separator, stream mode |
| `reader.hpp` / `writer.hpp` | the record-stream layer: type-erased `BasicReader`/`BasicWriter` over `istream`/`ostream`, format picked from the pack at runtime, `DelimitedString` separators, `WriteOrderHint` (FIRST/NEXT/LAST) for stream framing |
| `formatter.hpp` | the `std::formatter` bridge — `std::format("{:json}", x)`, `"{:yaml}"`, `"{:xml}"`, … (empty spec = pack default) |
| `delimited-string.hpp` | `DelimitedString<DELIM>`, a tag type carrying a compile-time `FixedString` delimiter for the record-stream layer |
| `errors.hpp` | the exception family — see "Errors" below |
| `io.hpp` | the adapters (`StringSink`/`StringSource`/`IstreamSource`/`OstreamSink`/`ByteSink`/`ByteSpanSource`), `TokenTraits` + `TokenStreamSource` (a string sequence of any character type as a streaming byte source, `docs/streaming-io.md` §4), the string / stream / file / string-sequence / argv entry-point forms, and the `ToJson`/`FromJson`/`ToYaml`/`FromYaml`/`ToXml`/`FromXml` shims |
| `canned-value.hpp` | `Canned` and `Uncan<T>` — a format-native subtree snapshotted on read and decoded later |
| `query.hpp` | dotted-path get/set/has over a reflected object, `FieldNames`, lookup by label as well as by member name |

`PositionalFormat::Reader::FieldNames()` returns an empty range. Its
`HasField` reports whether a child remains, and `EnterField` enters that
child regardless of the requested name. `query.hpp`'s by-name lookup uses
the reflected C++ object's scheme; those names remain available in memory
but cannot be recovered from positional bytes. Populated maps cannot
round-trip: their keys are field names, and the map reader enumerates
`FieldNames`.

## How a type says what it serializes as

One route: an ADL `reflect_scheme(T*)`.

| The type | How it gets a scheme |
|----------|----------------------|
| one you own | `friend constexpr auto reflect_scheme(T*);` in the class body, or beside the enum for an enum — the generator writes the definition from the declaration. `_Label(name)` at a declaration gives it an external name |
| one you don't own, or one the generator can't reach | the same scheme, written by hand in the type's own namespace where ADL finds it |

There is no macro layer and no member `scheme()`. A scheme is keyed on one
type: a derived type that declares no scheme of its own would still *answer*,
through the `Derived*`-to-`Base*` conversion, and the tier refuses that
borrowed answer (`_OwnsEveryItem`, `concepts.hpp`). A derived type reaches
its base's fields by naming it in its own scheme's `base_list`.

A scheme covers an ordinary type completely: every data member, private ones
included (the friend declaration granted exactly that visibility), doc
comments as field descriptions, and base classes — bases' fields first, in
declaration order, then the type's own.

### `_Label` — an external name

When the identifier cannot be the name the value carries outside the program
(`stdout`/`stderr` are `<cstdio>` macros; a private member's leading
underscore has no business on the wire), the declaration says so:

```cpp
struct Endpoint {
  friend constexpr auto reflect_scheme(Endpoint*);
  std::string host;
  _Label(secure) bool tls;   /* require TLS */
};

enum class Level { _Label(loud) VERBOSE, QUIET };
```

`tls` reaches the wire as `secure`, `VERBOSE` as `loud`. The marker expands
to nothing — the compiler never sees it, and the generator reads it out of
the raw token stream, which is what makes a declaration-site annotation
portable to toolchains with no attribute or reflection support. The argument
is unquoted, and a label the lexer would split (`content-type` is three
tokens) still arrives whole.

**A header that uses `_Label` must `#include <_buildutil/reflect.hpp>`**,
which is where the marker is defined.

A label is *empty* unless one was written, and empty is not the same as
"equal to the member's name": it means no external name was given, which
leaves each format free to derive one its own way. A label is taken
verbatim.

### A scheme written by hand

The vocabulary is the one the generator emits — `class_scheme` /
`derived_scheme` over `member_scheme`, `enum_scheme` over `enumerator` — so
a hand-written scheme and a generated one are the same object, found the
same way:

```cpp
namespace net {
  struct Endpoint { std::string host; bool tls{}; };

  constexpr auto reflect_scheme(Endpoint*) {
    using T = Endpoint;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"host", &T::host>,
      ::reflect::member_scheme<"tls",  &T::tls, "require TLS", false, "secure">
    >{ };
  }
}
```

After the pointer come the description (scheme metadata for schema
renderers, never on the wire — the generator fills it from the member's doc
comment), the encapsulation flag (read and ignored here: private state is
still state) and the label.

A scheme is a list somebody wrote, not an inventory of the type: it may name
fewer members than the type has. Every member it *does* name must belong to
the type the scheme is for — `&Derived::inherited` is a pointer-to-member of
the base and makes the scheme the base's, which the ownership guard rejects.

An enum maps to strings the same way. Each enumerator spells itself verbatim
(`TRACE` becomes `"TRACE"`) — serialization applies no naming convention of
its own — so one that must reach the wire under another name carries a
`_Label`:

```cpp
enum class LogLevel { _Label(trace) TRACE, _Label(info) INFO };
```

**What no scheme can say: a field with no data member behind it.** Computed
state addressed by a getter/setter pair has no pointer-to-member to name,
and `member_scheme`'s non-pointer form is an accessor yielding a
*reference*, which the tier reads as a reference member and drops. Such a
field does not serialize; back it with storage, or give the type an
`_Encode`/`_Decode` pair and put the computation there.

### Built-in primitives

`bool`, every `std::integral` and `std::floating_point`, `std::string` /
`std::string_view`, `std::filesystem::path`, `std::optional<T>`,
`std::unique_ptr<T>` / `std::shared_ptr<T>`, sequence containers (anything
with `begin`/`end` + `value_type` + `push_back`/`insert`), fixed-extent
sequences (`std::array<T, N>` — reads exactly N elements; a shorter wire
array is a `ParseError`, surplus elements are left unread), and maps with
`string_view`-convertible keys.

`std::filesystem::path` writes but does not read: `read-walker.hpp:65`
spells `PathFromString` unqualified and lookup at the point of instantiation
does not reach `oxbox::utilities`.

Absence against null, at field level: a `std::optional` field at `nullopt`
is absent from the wire, and readers accept absent or null as `nullopt`. A
smart-pointer field always appears, an empty one as an explicit `null`, and
readers accept absent or null as empty. On the wire, absence is the
optional's word and `null` is the pointer's. Inside a container a
`nullopt`/empty value still writes `null` — dropping it would lose the
element or the key. `PositionalFormat` also writes absent optional fields
as null nodes, preserving each field's position. A writer opts into this
walker behaviour with `static constexpr bool PRESERVE_FIELD_SLOTS{ true };`;
without that member, absent optional fields are omitted.

## Type inheritance

Reflection reaches base classes on its own; a derived type needs nothing
beyond its own friend tag.

```cpp
struct Foo {
  friend constexpr auto reflect_scheme(Foo*);
  int a;
  std::string b;
};

struct Bar : Foo {
  friend constexpr auto reflect_scheme(Bar*);
  double c;
};
```

`Bar`'s scheme is `Foo`'s fields followed by `Bar`'s own, and a round trip
covers all three. Multi-base is the same: every direct base contributes, in
declaration order.

A hand-written scheme says it the same way — the base goes in the
`base_list`, never into the member list:

```cpp
struct ChildT : ParentT { int c = 0; };

constexpr auto reflect_scheme(ChildT*) {
  return ::reflect::derived_scheme<
    ::reflect::base_list<ParentT>,
    ::reflect::member_scheme<"c", &ChildT::c>>{ };
}
```

`ParentT`'s own scheme is then asked for its fields, whatever they are
called there — a rename on the base arrives with them.

Rules that bite:

- Field order is bases-first in declaration order, then own fields. The
  on-wire byte order can still come out alphabetical, because
  `nlohmann::json` uses a `std::map` underneath; that is a JSON-DOM detail,
  not a scheme property.
- Each base is asked for its own scheme, and a base brings its own bases
  with it, all the way down.
- Only direct, public, non-virtual bases are captured (reflect's own limit,
  documented at `reflect::base_list`). An indirect base still arrives,
  through the direct one that derives from it.
- **A listed base with no scheme is skipped in silence.** That is deliberate
  — the cli's `Command` tag base is in the base list of every command and
  carries no fields — and the price is that an untagged base with real
  fields does not serialize, with no diagnostic. First thing to check when a
  field goes missing: does the base carry the friend tag?
- Field-name conflicts are laissez-faire. If a child shadows a parent field,
  both `Field` entries land in the merged scheme; on write the JSON DOM
  dedups by key (last wins), on read both write to their own slot. No
  `static_assert`, no warning.
- **A reference member is not serialized state.** It reflects through an
  accessor lambda rather than a pointer-to-member, and the tier drops it: no
  field, no error, never on a wire.

## Optional lifecycle hooks

Per type, opt in by member or by ADL free function. Detection is automatic,
via `HasArchiveHook<T>` / `HasRestoreHook<T>`.

| Hook | When | Signature |
|------|------|-----------|
| `_Archive` | before serialize — non-const, can flush caches or normalise | `void T::_Archive()` or `void _Archive(T&)` |
| `_Restore` | after deserialize — validate, recompute derived state | `void T::_Restore()` or `void _Restore(T&)` |

## Custom encode and decode

A type opts out of struct- or scalar-walking entirely with an
`_Encode`/`_Decode` pair. The wire form is whatever `_Encode` returns —
typically a `std::string`, for a type that has several fields but reads as
one primitive on the wire (a parsed URL, a strong-typed ID, a date).
`_Decode` takes the same wire type and returns `T`. Detection is automatic,
via `HasEncode<T>` / `HasDecode<T>`; when both are present they win over
`Scheme`.

| Form | Encode | Decode |
|------|--------|--------|
| Member | `auto T::_Encode() const -> W;` | `static auto T::_Decode(W) -> T;` |
| ADL | `auto _Encode(T const&) -> W;` (in T's namespace) | `auto _Decode(std::type_identity<T>, W) -> T;` (in T's namespace) |

```cpp
struct Url {
  std::string scheme, host, path;

  auto _Encode() const -> std::string {
    return std::format("{}://{}{}", scheme, host, path);
  }
  static auto _Decode(std::string s) -> Url {
    /* parse */ return Url{ };
  }
};
```

`Url` then serialises as `"https://example.com/api"`, a single string
scalar, rather than as an object. Whatever `_Encode` returns recurses back
through the walker, so any already-supported shape — scalar, array, map,
optional — is a valid wire type, and a `map<string, Url>` deserialises
through the map walker with the encode tier handling each value.

The `_Restore` name is also the discriminator hook for `std::variant`
(below). The two are disjoint by return type: the lifecycle hook returns
`void`, the discriminator hook returns the variant.

## Discriminated variants

A wire shape that differs by a tag field — GitLab webhook events keyed on
`object_kind`, say — is a `std::variant<Disc, T0, T1, …>` plus an ADL
`_Restore(Variant const&) -> Variant` in the namespace of the disc:

```cpp
namespace gitlab {
  struct EventDisc {
    friend constexpr auto reflect_scheme(EventDisc*);
    std::string object_kind;
  };
  struct PushEvent         { friend constexpr auto reflect_scheme(PushEvent*);         };
  struct MergeRequestEvent { friend constexpr auto reflect_scheme(MergeRequestEvent*); };
  struct IssueEvent        { friend constexpr auto reflect_scheme(IssueEvent*);        };

  using Event = std::variant<EventDisc,
                             PushEvent, MergeRequestEvent, IssueEvent>;

  inline auto _Restore(Event const& v) -> Event {
    if (auto const* d = std::get_if<EventDisc>(&v)) {
      if (d->object_kind == "push")          return PushEvent{ };
      if (d->object_kind == "merge_request") return MergeRequestEvent{ };
      if (d->object_kind == "issue")         return IssueEvent{ };
    }
    return v;   // unchanged, so the framework stops iterating
  }
}
```

A default-constructed `Event` holds an `EventDisc`; after a deserialize it
holds whichever alternative `_Restore` selected, or the disc itself if no
arm matched.

How it walks:

1. Read the currently-active alternative's fields out of the wire object —
   initially the disc's.
2. Call `_Restore` on the variant. If it returns a *different* alternative,
   replace the variant and re-read the same wire object into the new
   alternative's fields.
3. Repeat until `_Restore` returns the alternative it was passed.

Multi-level dispatch (disc to intermediate to final) falls out of the same
loop. If `_Restore` would revisit an alternative it already landed on during
this deserialize, the framework throws `ParseError`; the bound is the number
of alternatives.

The wire shape on disk is whatever the active concrete type serializes to —
no synthetic type-tag wrapper. Disc fields need not be repeated in a
concrete alternative's scheme (the reader only visits fields the scheme asks
for), but they may be, with no behaviour difference.

The write path is `std::visit` to the active alternative and dispatch to its
scheme; the discriminator hook is read-side only. Detection is
`HasVariantRestore<V>`, and a `static_assert` in the variant walker fires if
a variant is deserialized without one.

## Canned subtrees — partial decode

When a field's shape is not known at the outer decode layer — a per-module
config behind an encapsulation boundary — declare it as `Canned`
(`std::variant<YAML::Node, nlohmann::json>`):

```cpp
struct Outer {
  friend constexpr auto reflect_scheme(Outer*);

  std::string                   name;
  oxbox::serialization::Canned  module_config;     // decomposition halts here
};
```

`name` reads normally; at `module_config` the reader snapshots the
format-native node into the variant and walks no further. The value crosses
encapsulation boundaries still wrapped, and the TU that knows the concrete
type finishes the decode:

```cpp
auto outer  = oxbox::serialization::DeserializeFrom<Outer>(path);
auto config = oxbox::serialization::Uncan<ConcreteConfig>(outer.module_config);
```

`Uncan` constructs a Reader over the captured node (no re-parse) and runs
the standard read-walker dispatch — same `_Decode` hooks, same lifecycle
hooks, same exception types as a plain `Deserialize<ConcreteConfig>`.

Decode-only: there is no `Can(...)` and no encode-side support.

## Entry points

All in `io.hpp`. `F` is a Format type tag.

| Form | Signature |
|------|-----------|
| Sink (primary) | `Serialize<F>(T&, Sink&)` |
| Source (primary) | `Deserialize<F, T>(Source&) -> T` |
| String | `Serialize<F>(T&) -> std::string` / `Deserialize<F, T>(string_view) -> T` |
| Istream | `DeserializeFrom<T>(istream&, F = {}) -> T` |
| Ostream | `SerializeTo<F>(T&, ostream&)` |
| Path (extension dispatch) | `DeserializeFrom<T>(path) -> T` / `SerializeTo(T&, path)` |
| String sequence / argv | `DeserializeFrom<T, F>(range, TokenTraits = {})` / `DeserializeFrom<T, F>(argc, argv, TokenTraits = {})` — the format is named explicitly, there is no default |
| Format-specific | `ToJson`/`FromJson`, `ToYaml`/`FromYaml`, `ToXml`/`FromXml` |

Path-form dispatch goes through `FormatFromExtension`, which walks
`AllFormats` in order and takes the first format claiming the extension:
`.json`, `.yaml`/`.yml`, `.xml`, `.bsx`, `.bsp`. Anything else throws `ParseError`.
`XmlPrettyFormat` claims `.xml` too, but `XmlFormat` precedes it in the
pack, so pretty is reachable only by name — `FormatFromString("xml-pretty")`
— which is also `DEFAULT_FORMAT_NAME`.

## Errors

All in `errors.hpp`, all derived from `std::runtime_error` and so from
`std::exception`: one `catch (std::exception const&)` at the tool boundary
picks up every one.

| Exception | Thrown when | Constructor |
|-----------|-------------|-------------|
| `ParseError` | malformed wire, unknown enum string, unknown file extension | `(std::string what)` |
| `MissingField` | a required Scheme field — neither optional nor pointer — absent on the wire | `(std::filesystem::path field)` |
| `TypeMismatch` | the wire value's type is not the expected one | `(std::filesystem::path where, std::string expected)` |
| `FileOpenError` | path-form open failed | `(std::filesystem::path path)` |
| `InvalidArgument` | the caller passed an argument the serializer cannot accept | `(std::string source, std::string reason)` |

Both binary formats share a `Cursor` that treats a blob as untrusted input
and raises `ParseError` for truncation, an over-long length, an unknown tag, a varint
wider than the 64 bits it can carry (protobuf's ten-byte rule), and a child
escaping its container — object field or array item alike, checked before
the child is read.

## Wire shapes worth knowing

- **XML.** `<root type="object">` wrapping field elements named by their
  wire key; an array is `<root type="array">` with `<item>` children; a
  scalar is text content under a
  `type="string|int|uint|number|bool|null"` attribute. Compact emits one
  line per record (`format_raw`), pretty indents (`format_indent`); both
  separate records with `<!-- // -->\n`, so a concatenated stream stays
  valid XML. Record separators per format are in `format-traits.hpp`:
  `"\n"` for json, `"---\n"` for yaml, none for binary or positional.
- **Binary.** One tag byte per node, varint integers, length-prefixed
  strings. `.bsx`, opened in binary mode so the bytes survive untranslated.
- **Positional.** The binary tags and scalar encodings above, with no names.
  An object is tag 5, its payload byte length as a tagged integer, then
  a child count as a tagged integer followed by children in `Field` order.
  Arrays retain the binary byte-length envelope without a child count.
  The count must consume exactly the object payload. `.bsp`, binary stream
  mode, no record separator. Schemes must agree on field order, including
  discriminator fields when variants re-read an object.

## Adding a new format

A Format is a class with two nested class templates:

```cpp
struct TomlFormat {
  template <serialization::Sink S>
  class Writer {
    // Write(bool/int64_t/uint64_t/double/string_view), WriteNull,
    // BeginObject/Field/EndObject, BeginArray/EndArray, Flush.
    // Flush drains the finished wire through detail::PutText(sink, out).
  };

  template <serialization::Source Src>
  class Reader {
    // Read<T>(), IsNull, Subtree,
    // EnterObject/HasField/FieldNames/EnterField/LeaveField/LeaveObject,
    // EnterArray/HasNext/EnterNext/LeaveNext/LeaveArray,
    // Path() -> std::filesystem::path const&.
  };
};
```

Conformance is pinned at compile time in `unit.test/format.cpp`:

```cpp
static_assert(serialization::Format<TomlFormat>);
static_assert(serialization::WriterBackend<TomlFormat::Writer<StringSink>>);
static_assert(serialization::ReaderBackend<TomlFormat::Reader<StringSource>>);
```

Then register it: add it to `AllFormats` in `format-pack.hpp` (the single
declaration site) and give it a `FormatTraits` specialization in
`format-traits.hpp` — name, extension claims, separator, stream mode.
By-name lookup, path-extension dispatch, the record-stream layer and the
`std::formatter` bridge all pick it up from there.

Adding it to the `AllFormats` type list in `unit.test/format.cpp` runs the
common contract tests. `NamedFormatContract` holds populated-map cases;
`TextFormatContract` holds text corruption and mapped enum-name cases;
`BinaryFormatEnums` covers integer enums for both binary formats.
Format-specific wire-shape tests go in a `unit.test/format-toml.cpp`.

## Adding a scheme to a type you can't edit

You cannot befriend somebody else's class, so its scheme is written from
outside, in the type's own namespace where ADL finds it:

```cpp
namespace ext {
  struct Point { int x, y; };

  constexpr auto reflect_scheme(Point*) {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"x", &Point::x>,
      ::reflect::member_scheme<"y", &Point::y>>{ };
  }
}
```

Private members are out of reach this way: no friendship, no access.

## Testing surface

| File | Covers |
|------|--------|
| `unit.test/serialization.cpp` | format-agnostic behaviour: Scheme dispatch (missing field, type mismatch, const overload, the generic `Serialize<F>` entry point), short wire arrays into tuples, and encapsulated types — private and protected state on the wire, labels as wire names, and the pin that no scheme declared is no scheme at all |
| `unit.test/reflected-scheme.cpp` | the scheme route end to end: the friend tag alone in every format, private members, doc comments as descriptions, enums and enumerator labels, bases (hand-written, grandbase, untagged), reference members, a scheme naming fewer members than its type has, and the pin that an untagged derived type cannot borrow its base's scheme |
| `unit.test/inheritance.cpp` | single-base, multi-base, three-deep chains, a hand-written scheme reaching its base through `base_list`, variant-alternative-inherits-disc, field-name-conflict laissez-faire, vector-of-derived |
| `unit.test/discriminated-variant.cpp` | round-trips, multi-level dispatch, cycle detection, nested-in-scheme, vector-of-variant |
| `unit.test/encode-decode.cpp` | the `_Encode`/`_Decode` hook in both spellings, the scalar wire shape it produces, encoded fields inside a schemed struct, and its ordering against `_Archive`/`_Restore` |
| `unit.test/canned-value.cpp` | `Canned` subtree capture and `Uncan` completion |
| `unit.test/io.cpp` | each adapter, every entry-point form, file-extension dispatch, error paths (unknown extension, missing file) |
| `unit.test/format.cpp` | contract conformance — `static_assert` per (format, adapter) pair, plus a `TYPED_TEST_SUITE` against every format. `TextFormatContract` holds the cases that corrupt the wire as text or rely on enum names, which the binary wire has neither of; `NamedFormatContract` holds populated-map cases; `BinaryFormatEnums` pins both binary formats on the same enum inputs |
| `unit.test/format-json.cpp` / `-yaml` / `-xml` / `-binary` / `-positional` / `-node` | per-format wire shape and parser behaviour; binary also covers malformed-input rejection |
| `unit.test/reader.cpp` / `unit.test/writer.cpp` | the record-stream layer: per-format framing, delimiters, order hints |
| `unit.test/formatter.cpp` | `std::format` spec dispatch — `{:json}`, default, unknown-name error |
| `unit.test/query.cpp` | dotted-path get/set/has, `FieldNames`, lookup by label rather than member name |
| `unit.test/native-shortcut.cpp` | a backend `Read`/`WriteNativeCapable` for a `T` is handed the whole value, no decomposition; a bare format decomposes the same type identically |
| `unit.test/headers.cpp` | every public header of this module compiles on its own |
