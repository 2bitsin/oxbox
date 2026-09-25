# oxbox

A C++ library in five modules, shipped as a conan package: `cli` for
command lines, `serialization` for wire formats, `http` for a client and a
server, `platform` for files and memory, and `utilities` for the byte, text
and number tools the rest is built on.

Header-only except where a table below says otherwise; `cli`, `http` and
`platform` compile to archives.

Headers ship qualified:

```cpp
#include "oxbox/serialization/format-json.hpp"
#include "oxbox/utilities/unicode.hpp"
```

Namespace is `oxbox::<module>`; implementations live in
`oxbox::<module>::detail::<file>` and are re-exported by name.

## Modules

### `cli`

Declarative command lines: a `Command` struct whose public members are the
options, whose methods and member Commands are the subcommands, and whose
doc comments are the help text — reflected by the same machinery as
serialization, dispatched by `cli::Main<App>(argc, argv)`. Help joins single
newlines in comments into spaces and folds each paragraph to the available
width. A blank line separates paragraphs.

Help distinguishes value options (`--url <string>`, `--port <int>`) from
bool flags (`--verbose`). Optional values use their contained type. Enum
options use the option name (`--tint <tint>`), with accepted spellings in
the detail column so a long enum list does not widen every row.

An option may also have a one-character short spelling from a meta tag:

```cpp
bool verbose{ } _Meta("-v"); /* say what is being done */
```

Place `_Meta` before the semicolon so reflection attaches it to the member.
`-v` uses the same flag behavior as `--verbose`; value options accept
`-o value` and `-o=value`. Short options are case-sensitive and cannot be
bundled (`-abc` is unknown). A lone `-` remains an operand; `--` still ends
option parsing. Dash-prefixed tags must contain exactly two characters,
and two members of one command cannot claim the same short spelling; both
are compile-time errors. Other tags are untouched. Help lists
`-v, --verbose`; the built-in help accepts and lists `-h, --help` unless a
member claims `-h`. A command declaring `help` owns help handling entirely.

A member's external name is its `_Label` when it carries one
(`_Label(secure) bool tls;` is `--secure`) — on an option, a subcommand
member, a subcommand method, an enum value (`DEEP_BLUE _Label(ocean)` is
`--tint=ocean`) and an `operator()` parameter (`_Label(page) std::string
url` is `<page>`) alike. `_Label(--)` is a marker rather than a name: on a
`std::vector<std::string>`, `std::vector<std::string_view>` or
`std::string` member it makes that member the rest collector, which
receives every token after a bare `--` verbatim and in order instead of
those tokens becoming positionals.

| Header | Holds |
|--------|-------|
| `command.hpp` | `Command`, the base every layer derives from, `CliResult`/`CliStatus`, `InlineSegment` |
| `errors.hpp` | the usage-error family, each carrying the text it objected to |
| `naming.hpp` | how a declared name is spelled on the line, and what counts as a match |
| `scheme.hpp` | the flattened member list a reflected command presents, bases first, and `HasMemberList` |
| `member-shape.hpp` | what shape a member's type has: how many tokens it takes, how a value goes in |
| `escaping.hpp` | the backslash rule for a value's text, and splitting on unescaped separators |
| `member-role.hpp` | what a reflected member or method is to the line: option, subcommand, rest collector, action, lifecycle hook |
| `rest-collector.hpp` | finding the `_Label(--)` member and delivering the tail after `--` to it |
| `name-lookup.hpp` | matching a typed word against the names a command declares |
| `one-name-per-declaration.hpp` | the compile-time guard that two declarations never answer to one name |
| `convert.hpp` | a token into a member's type, and the one place an unsupported type is named |
| `range.hpp` | `AnyView`/`AnyIterator`, the type-erased tail a variadic positional binds to |
| `parse.hpp` | the option loop over one segment, and the `Outcome` it answers with; the module entry point |
| `invoke.hpp` | binding what is left of the line to an `operator()` or a subcommand method |
| `help.hpp` | the help screen, for one layer or for a whole chain |
| `main.hpp` | the entry points, and the two-pass walk that judges a line before any of it runs |
| `console.hpp` | `PrepareConsole`/`SetStdoutUnbuffered`/`AttachParentConsole` |
| `owned-argv.hpp` | `OwnedArgv`, argv as a C API keeps it |

`console` and `owned-argv` are what an entry point does before it reads a
line. `PrepareConsole` makes stdout unbuffered — under a pipe a result line
can die in the buffer, and it must be `_IONBF`, never `_IOLBF`, which is a
hard error in the Microsoft CRT — and attaches the parent console on win32,
a documented no-op elsewhere. `OwnedArgv` hands out a null-terminated
`char**` over strings it owns; copy and move are deleted because either
would leave a `char*` aimed at a string that had gone. Its natural source is
the rest collector.

Compiled: `console.cpp` plus `console.posix.cpp` / `console.win32.cpp`.

### `serialization`

Round-trip user types against wire formats. A type opts in with a
`friend constexpr auto reflect_scheme(T*);` tag, or with the same scheme
written by hand beside a type you do not own. Six formats are registered
in `AllFormats`: `json`, `yaml`, `xml`, `xml-pretty` (the pack default),
`binary` and `positional`. Backends: nlohmann-json, yaml-cpp, pugixml.

`sources/oxbox/serialization/README.md` is the module's own reference — the
scheme vocabulary, the hooks, the entry points, the error family, and how to
add a format. The header table:

| Header | Holds |
|--------|-------|
| `concepts.hpp` | `Sink`, `Source`, `Character`, `Writer`, `Reader`, `Format`, and the `Has*` detection concepts |
| `stream.hpp` | the contract plumbing: drain loops for writers, a `std::streambuf` over a `Source`, binary's blob pull |
| `scheme.hpp` | the declarative vocabulary: `Field`, `Scheme`, `EnumMap`, and the traits the field machinery dispatches on |
| `hooks.hpp` | the per-type opt-in points: the canned-subtree marker, and the archive/restore lifecycle calls |
| `reflected-scheme.hpp` | turning what `reflect_scheme(T*)` emits into a `Scheme` or an `EnumMap`, plus `SchemeFor`/`EnumMapFor` |
| `write-walker.hpp` | walking a scheme against a writer backend, one `Visit` overload per shape; absent-versus-null is settled here |
| `read-walker.hpp` | the read half of the same contract |
| `query.hpp` | `FieldInfo` and `WithFieldNamed`: reaching one field of a scheme by name |
| `serializable.hpp` | the module entry point: includes the five above, holds `Serialize<F>`/`Deserialize<F, T>` |
| `format-json.hpp` | `JsonFormat` over nlohmann::json |
| `format-yaml.hpp` | `YamlFormat` over yaml-cpp |
| `format-xml.hpp` | `XmlFormat` and `XmlPrettyFormat` over pugixml |
| `binary-wire.hpp` | shared tags, varints and the bounds-checked `Cursor` for both binary formats |
| `binary-reader.hpp` / `binary-writer.hpp` | shared binary navigation and emission, with named or positional objects |
| `format-binary.hpp` | `BinaryFormat`, a tagged binary wire with named fields |
| `format-positional.hpp` | `PositionalFormat`, a tagged binary wire with fields in scheme order and no names (`.bsp`) |
| `node.hpp` / `format-node.hpp` | `Node`, an owning in-memory DOM, and `NodeFormat` over it |
| `format-pack.hpp` | `FormatPack`, the by-name/by-extension registry; `AllFormats` is the declaration site |
| `format-traits.hpp` | per-format name, extension claims and stream mode |
| `reader.hpp` / `writer.hpp` | the record-stream layer: type-erased `BasicReader`/`BasicWriter` over `istream`/`ostream` |
| `formatter.hpp` | the `std::formatter` bridge — `std::format("{:json}", x)` |
| `delimited-string.hpp` | `DelimitedString<DELIM>`, the record-stream layer's compile-time delimiter |
| `errors.hpp` | the module's exception family |
| `canned-value.hpp` | `Canned` and `Uncan<T>` — a subtree snapshotted format-native and decoded later |
| `io.hpp` | the adapters (`StringSink`/`StringSource`/`IstreamSource`/`OstreamSink`), `TokenTraits` + `TokenStreamSource`, and every entry-point form |

### `http`

A minimal HTTP/1.1 client and server over boost::asio, one concern per
header. Everything but `connection`, `response-stream` and `server` is pure
byte work with no I/O in it. Deps: boost (header-only) and openssl.

Builds on native targets only (Windows, Linux and Darwin). On Emscripten,
the module and its `oxbox::http` package component are absent; including
`fetch.hpp` is refused at compile time with "oxbox::http is not built for
the browser: Boost.Asio has no Emscripten transport".

| Header | Holds |
|--------|-------|
| `asio.hpp` | the module's one door to boost, and the `_WIN32_WINNT` floor that keeps every TU's asio the same shape |
| `url.hpp` | an absolute http(s) url as a value (RFC 3986 §3, RFC 9110 §4.2, §7.2) |
| `delivery.hpp` | the seam a body crosses to become the encoding a caller asked for |
| `error.hpp` | `TransportError`, the module's one exception: a transfer that never produced a complete response (404 and 500 are delivered normally) |
| `connection.hpp` | the byte pipe: TCP or TLS against a deadline, the certificate verified against the system trust store and against the name asked for, which is sent as SNI |
| `fetch.hpp` | one exchange as a coroutine that yields body bytes and returns the response (RFC 9110 semantics, RFC 9112 syntax) |
| `server-message.hpp` | `ServerRequest` and `ServerResponse` — one request as it arrived and one response as it will be written, over the client half's field table, method, media type and payload |
| `response-stream.hpp` | `ResponseStream`, a response written over time and framed by chunked transfer coding (RFC 9112 §7.1): the head once, then chunks, then the end |
| `router.hpp` | `Handler`, `StreamHandler` and the exact method-and-path match behind 404 and 405 |
| `server.hpp` | `Server` — binds while it is built, accepts on the caller's executor, and stops without cutting an open stream |

Compiled: `connection.cpp`, `delivery.cpp`, `fetch.cpp`, `response-stream.cpp`,
`router.cpp`, `server-message.cpp`, `server.cpp`, `url.cpp`.

### `platform`

The host's memory and files, as RAII. Each entry point is a free-function
substrate with an owning view over it; every failure is a
`std::runtime_error` naming the module, the call, the path and what the OS
said. Anything that creates a file and then fails takes that file with it,
and never touches one it did not create. The contract facility lives here
too, because where a failed contract is said is the host's business.

| Header | Holds |
|--------|-------|
| `contract.hpp` | `ContractMode` and `Contracts<MODE>` — `Expects`/`Ensures`, `Unreachable(value)`, `NotImplemented`, the one line a failure says, and `ContractFailure`, that line as an exception |
| `contract-report.hpp` | where that line goes, split by tag: stderr natively, the browser console in the wasm lane. Not re-exported |
| `memory.hpp` | `AllocatePages`/`ReleasePages`, and `PageAlignedArray<T>` — an owning page-aligned array of a trivial type that copies as a real allocation |
| `native-file.hpp` | the OS handle and its verbs. Not re-exported, which is what keeps it private to the module |
| `native-file.posix.hpp` / `native-file.win32.hpp` | each road's own vocabulary, included only by that road's `*.cpp` |
| `mapped-file.hpp` | `MapFile`/`UnmapFile`/`FlushMapping`/`SyncFile`, `MapFileForWrite`, and `MappedFile`, the owning move-only view |
| `file-writer.hpp` | `WriteBinaryFile` for a scratch dump, and `FileWriter` for a save that renames a temporary over the target |
| `scratch-area.hpp` | `ScratchArea`, a directory nothing else is using, removed recursively when it goes out of scope |

**Contracts are bound to the consumer's own build option, never to
`NDEBUG`.** `Contracts<MODE>` takes the mode as a template argument, so a
project binds it once and names the binding everywhere:

```cpp
using Contract = oxbox::platform::Contracts<MYPROJECT_CONTRACT_MODE>;

Contract::Expects(count <= wire.size(), "the count fits the wire");
switch (kind)
{
  case Kind::HEADER:  return ReadHeader(wire);
  case Kind::PAYLOAD: Contract::NotImplemented("the payload road");
  default:            Contract::Unreachable(kind);
}
```

`ContractMode` has four values, and each kind answers a broken contract
its own way:

| | `STOP` | `THROW` | `COMPLAIN` | `IGNORE` |
|--------|--------|---------|------------|----------|
| `Expects` / `Ensures` | says the line, `std::abort()` | says the line, throws | says the line, returns | nothing |
| `Unreachable(value)` | says the line, `std::abort()` | says the line, throws | says the line, `std::abort()` | `std::unreachable()` |
| `NotImplemented(text)` | says the line, `std::abort()` | says the line, throws | says the line, returns | returns |

`Unreachable` stops in `COMPLAIN` too, because a closed switch's default
has no continuation to return to, and it stays `[[noreturn]]` in every
mode — `THROW` leaves it by throwing.
`NotImplemented` is not `[[noreturn]]` — the attribute cannot depend on the
mode — so a site that calls it must have a defined continuation after it.
A site that complains says its line every time it is hit; throttling that
is the consumer's business.

`THROW` is for a host with somewhere to put the failure: the line is said
first, so a console keeps it whether or not anything catches, and then a
`ContractFailure` — a `std::runtime_error` whose `what()` is that same
line, carrying `Kind()`, `Text()` and `Where()` on their own — travels to
the handler. A game's betas build with it and halt on a screen showing the
line rather than walking past the hole.

The line is the same in every mode:
`precondition: <file>:<line> <function>: <text>`, on stderr natively and on
the browser console in the wasm lane. The checks run in every build type,
release and the betas included; they are not the debug flag's and not
`NDEBUG`'s. A game's betas run in `COMPLAIN`, so a player walks past a hole
with the console saying what was hit, and its tests run in `STOP`.

The mode comes from the consumer's own buildutil option — `[options]
contracts = "stop"` in its `buildutil.toml`, `--option contracts=complain`
for one build. Every target of that project compiles with the value as a
string literal, and a `consteval` mapping turns it into a mode: any other
word fails a `static_assert`, and a macro nobody declared is a compile
error rather than a quiet default:

```cpp
consteval auto ContractModeNamed(std::string_view name)
  -> std::optional<oxbox::platform::ContractMode>
{
  using oxbox::platform::ContractMode;
  using namespace oxbox::utilities::literals;
  switch (oxbox::utilities::HashString(name))
  {
    case "stop"_hash:     return ContractMode::STOP;
    case "complain"_hash: return ContractMode::COMPLAIN;
    case "off"_hash:      return ContractMode::IGNORE;
    default:              return std::nullopt;
  }
}

inline constexpr auto CHOSEN{ ContractModeNamed(MYPROJECT_CONTRACTS) };
static_assert(CHOSEN.has_value(), "contracts is stop, complain or off");
inline constexpr auto MYPROJECT_CONTRACT_MODE{ *CHOSEN };
```

Four facts a caller needs:

- `MappedFile::Flush()` is durable on both roads. POSIX gets that from
  `msync(MS_SYNC)`; win32 needs `FlushViewOfFile` *and* `FlushFileBuffers`,
  so a shared-write mapping there keeps its file open for the second half.
- `Backing::SPARSE` on the create-at-size road asks the filesystem for
  holes. It is a documented no-op on POSIX; on NTFS it is `FSCTL_SET_SPARSE`
  between the create and the size, without which giving a file 8 GiB and
  touching the last byte writes 8 GiB of real zeros. What the host agreed to
  comes back from `MappedFile::BackingInForce()`, so a consumer on a volume
  with no sparse files can warn before the zero-fill rather than after.
- `FileWriter` saves durably at both ends (the bytes before the rename, the
  directory entry after it), carries the target's mode across, and a failed
  `Write()` kills the writer so a partial document can never reach a good
  file. `Commit()` throws `SavedButNotDurable`, and only that type, when the
  document landed and only its name's durability is unknown; `Committed()`
  answers the same question for a caller that caught something broader.
- **On Windows the caller must drop its `MappedFile` before `Commit()`** — a
  live section blocks the replacing rename, where POSIX does not care. No
  API here can enforce it.

`ScratchArea` takes a per-process token and a per-area serial, and uses
`create_directory` rather than `create_directories`, so a path that already
exists is a refusal rather than a directory shared with whoever made it. It
is what every parallel test lane and every scratch file should name a path
with; this module's own `TempPath` fixture is a name inside one.

Not built yet, with the reason, at the top of `mapped-file.hpp`: resizing a
live mapping, offset windows, madvise hints.

Compiled: `file-writer.cpp`, `scratch-area.cpp`, and the `.posix`/`.win32`
halves of `mapped-file`, `memory` and `native-file`.

### `utilities`

The header set the other modules depend on, plus the byte and number tools
projects each had a drifting copy of.

| Header | Holds |
|--------|-------|
| `short-types.hpp` | `U08`…`U64`, `S08`…`S64`, `F32`/`F64`, `Bytes`/`WritableBytes`, and the `*MAX` constants |
| `span.hpp` | `SafeSubspan`, `Advance`, `SpanCast`, `AsBytes`/`AsWritableBytes`, `BytesEqual` |
| `ranges.hpp` | `get<N>` — `std::get<N>` as a callable, for use as a range projection |
| `visitor.hpp` | `Visitor`, the overload-set aggregate for `std::visit` |
| `fixed-string.hpp` | `FixedString`, a string usable as a template argument |
| `errors.hpp` | the project's exception family |
| `exception.hpp` | `Exception<ID, Base, FORMAT, Args...>`, one exception type per `using` alias, its text formatted from checked arguments |
| `text.hpp` | `Joined` (with optional projection), case folding and trimming. Text in, text out |
| `string.hpp` | `CompatibleStringView`, `StringLikeValue`, `StringSequence`, `StringRange` — what counts as a string, and as a range of them |
| `path.hpp` | `PathFromString`/`PathToString` — a filesystem path across the char/wchar boundary without a locale in the way |
| `codepoint.hpp` | what a code point is and whether it is one: the widths, `Encoding`, the range constants, the validity rules |
| `utf-encode.hpp` | a code point down into code units of a named width |
| `utf-decode.hpp` | code units back up into a code point, one unit at a time, with the carry state |
| `codepoint-bytes.hpp` | one code point across the byte boundary at a named encoding and byte order |
| `buffer-decode-iterator.hpp` | walking a byte buffer as the code points it holds |
| `buffer-encode-iterator.hpp` | writing code points into a byte buffer, with the shortfall and the refusal latch that say what did not fit |
| `unicode.hpp` | the entry point that includes the six above, and `UtfTranscode`, the one crossing that needs both directions |
| `transcode.hpp` | the tier above: `SniffByteOrderMark`, `DecodeResilient`, `EncodeAppend`, and the bounded-carry `ChunkDecoder` |
| `serdes.hpp` | `Fetch`/`Store`, `BoundedReader`/`BoundedWriter`, and `GrowingWriter<endian>` for integral/enum scalars, zero fill, bytes and raw text in an owned or supplied vector |
| `hash.hpp` | FNV-1a at 32 and 64 bits, over bytes or text, with a running parameter for a digest over a stream; `HashString` and the `_hash` literal that make a string a switch case label |
| `hex.hpp` | bytes to hex text and back off one nibble table |
| `number-text.hpp` | `ParseNumber` with radix markers and `AsWritten`, `ParseNumbers` for fixed-size separated fields, and `ParseNumberAfter` for a marked token; `FormatNumber`/`HexText` the other way |
| `bits.hpp` | `AlignUp`/`IsAligned` in both spellings, the width-to-type maps (`UIntOfSize`, `UIntOfAtLeastLength`, …), `IntExtend`, and `ExtractBits`/`ReplaceBits`/`ExplodeBits`/`CompactBits`, every mask full-width safe |
| `take-once.hpp` | a value held for exactly one reader. No `Empty()`, deliberately |

The octets-to-text tier uses `TextFormat` to name the encoding and byte
order; the default is native, not RFC 2781 section 4.3's big-endian.
`ByteOrder::READ_MARK` asks `SniffByteOrderMark` to read the encoding's own
mark, consume it and settle the order; no mark leaves native order.
A stated big- or little-endian order wins and consumes nothing: a leading
U+FEFF is text. `DecodeResilient` repairs malformed input, `EncodeAppend`
writes code points, and `ChunkDecoder` carries incomplete sequences. With
`READ_MARK`, it also holds up to four octets across feeds to settle a mark;
unmarked input decodes in native order, including short input at `Finish()`.

Three of those carry a contract worth stating here:

- **`serdes`** has two. The free `Fetch`/`Store` throw on a short span, and
  are for a caller that already knows the buffer is long enough — there the
  exception is the assertion that says so. `BoundedReader`/`BoundedWriter`
  are for bytes that arrived from somewhere: the failure is sticky, every
  step after an overrun answers with a default and latches, and `Sound()` at
  the end is the one place that decides. A count is checked against what is
  left and never by adding it to the cursor, and the writer's padding is
  written rather than skipped.
- **`hex`** answers `std::optional` from `BytesFromHex`, `HexByteCount` and
  `BytesFromHexInto`, and `HexFaultIn` names what a refusal objected to for
  a caller that must explain it. Two consteval forms exist for fixtures:
  `HexBytes<"4d 5a">()` and `0xDEADBEEF_hex`. The text form is a function
  template, not a `_hex` string literal, because cl cannot resolve two
  literal operator templates of one name.
- **`bits`**'s `AlignUp` comes in a runtime-boundary form with a
  power-of-two fast path and a `AlignUp<BOUNDARY>(T)` form that is a fact
  about the code, checked at compile time.

`Exception` is a type named by a `using` alias rather than a class written
per failure: `using PortRefused = Exception<"PortRefused"_hash,
std::runtime_error, "port {} refused by {}", std::uint16_t, std::string>;`
then `throw PortRefused{ port, peer };`. The format is checked against the
argument types at compile time wherever the type is completed (thrown,
caught, constructed), so a field without an argument or a presentation the
type does not have is a build error; the exception is a dynamic width or
precision, which throws `std::format_error` at construction. The id, by
convention `HashString` of the alias name, keeps two aliases of one base
and format distinct types that catch apart. A child of
`runtime_error`/`logic_error` constructible from `std::string` is handed the
text, and `what()` is what that constructor makes of it: the text itself for
the nine classes [std.exceptions] defines. Any other default-constructible
base gets it held shared and immutable behind `what()`, and a moved-from
exception still answers `what()`. A copy is `noexcept` exactly when the
base's is. A final base, or one that cannot be copied or copy-assigned, is
refused; a base whose `what()` is final is a compile error unless the base
is such a child.

## Consuming

In `sources/CMakeLists.txt`:

```cmake
Require(oxbox VERSION ">=0.22.0.267" CONAN oxbox)
```

and in each consuming module's CMakeLists:

```cmake
Link_dependencies(oxbox::oxbox)          # or a component: oxbox::cli, oxbox::utilities
```

**That version is a floor, not a pin.** `conanfile.py` widens `>=0.x` to the
conan range `[>=0.x <1]`, so a consumer floats to the newest 0.x it can see —
and a stale local cache satisfies a loose range forever without re-checking
the remote. Leave the floor equal to the version the tree actually requires;
that is what turns a too-old resolution into a loud failure instead of a
silent one.

The package ships prebuilt binaries only: the recipe refuses cache source
builds and prints what to look at. `oxbox` has no `package_id()` override,
so `compiler.cppstd` is part of its package id — a consumer on a cppstd no
lane published finds no binary. Building consumers needs the project conan
remote configured (`CONAN_REMOTE_URL`/`_USER`/`_PASS` in `.env`, or the
`CI_ARTIFACTORY_*` spellings). If a consumer lands in the refusal, read its
message first: the common cause is that its platform or build type was never
published.

Boost and OpenSSL are public dependencies of oxbox (the `http` module), so
they arrive in a consumer's graph whether or not it calls `http`.

## Building and testing

The toolchain floor is gcc 16 with its libstdc++ (the code uses
`std::ranges::starts_with`, `append_range` and range formatters, which
libstdc++ 15 lacks) or an equivalent clang with libc++; cmake, ninja and
python 3.11 or newer; and clang on PATH for the reflect extension, whose
libclang wheel carries no builtin headers of its own. Dependencies resolve
from Conan Center; nothing else is needed.

```
./buildutil build --skip-dependency-upload-so-everyone-rebuilds-from-source   # first run bootstraps _pyvenv
./buildutil test  --skip-dependency-upload-so-everyone-rebuilds-from-source   # unit tests + the package test
```

**Both `build` and `test` upload by default here** — they end with
`conan upload "*"`, which republishes oxbox itself and stacks a new recipe
revision under an already-released version. Always pass
`--skip-dependency-upload-so-everyone-rebuilds-from-source` locally (buildutil 0.75.1
renamed `--no-upload` to that on build, test, run and bench; a bare
`--no-upload` is refused with the new name). Only the publish lanes may
upload.

CTest discovers 1115 cases, including the short-option compile refusals.
Runtime unit tests travel with the modules, as `.cpp` files under each
module's `unit.test/`; `test_package/smoke.cpp` proves the shipped package
resolves, compiles, links and runs a real roundtrip.

### `tools/negative-compile.sh`

Some of what this library promises is a refusal, and gtest cannot hold a
build that does not happen. This script compiles fixtures that must not
compile and checks each diagnostic carries the guard's own sentence.

```
tools/negative-compile.sh                   # every case, gcc + clang + cl
tools/negative-compile.sh --no-msvc         # the two host compilers only
tools/negative-compile.sh --case hex        # rows whose name matches
tools/negative-compile.sh --fixture FILE --expect TEXT [--spelling NAME]
tools/negative-compile.sh --list            # units: each refusal, each control once
tools/negative-compile.sh --compiler gcc --unit short-options.distinct
```

The case table is in the script. Each row names a fixture that must be
refused, a fragment of the message the diagnostic must carry, a name the
message must lead the reader to (or nothing), and a control that must still
compile — a check that only ever saw a failing compile cannot tell a working
guard from a broken include path. The cases cover duplicate long names,
duplicate short spellings (checking both member names), malformed short
tags, invalid hex literals, and exception formats their arguments do not
fit. Native Linux CTest also runs every case in the
table with the host compilers, one entry per unit and compiler
(`negative-compile.gcc.short-options.distinct`), so each entry is one
compile and a parallel gate spreads them; the standalone script adds the
MSVC leg. A run whose every selected leg was skipped exits 77, which CTest
reports as skipped.

To watch it report failure, point it at a control:

```
tools/negative-compile.sh --fixture \
    tools/negative-compile/one-name-per-declaration.distinct.cpp \
    --expect "answer to the same command-line name"
```

It needs a build tree for one generated header (`<_buildutil/reflect.hpp>`,
which `parse.hpp` includes) and stops with instructions if there is none, so
run `./buildutil build --skip-dependency-upload-so-everyone-rebuilds-from-source`
first. The fixtures live in
`tools/negative-compile/`, outside `sources/`, because the root CMakeLists
adds `sources/` and nothing else: a file the ordinary build must never
compile has no business in a compiled `*.test/` subtree. They hand-write
their schemes, because outside `sources/` the reflect generator never sees
them.

The cl leg runs in a docker image named by `OXBOX_MSVC_IMAGE`, which the
script requires and has no default for. It is the only cover the one-name
guard's `#else` branch has. gcc and clang have P2741R3 and get the colliding
spelling composed into the `static_assert` message; cl 19.51 does not
(`__cpp_static_assert` is `201411L` there), so the spelling rides in a
template argument and cl prints it as decimal character codes while
instantiating the guard:
`std::array<char,13>{_Ty99,111,110,116,101,110,116,45,116,121,112,101,0}`.
That is a pass with a note, not a failure — the refusal fires, the message
is right, and the name is carried complete and in order. What stays red is a
guard that does not lead a reader to the collision: no refusal, the wrong
message, or the spelling in neither form. The cl leg skips loudly, naming
the branch it left uncovered, on a machine with no docker or no image.

## Releasing

**The version is the git tag.** There is no version file: `conanfile.py`'s
`set_version` adopts whatever buildutil passes. A bare-semver tag `X.Y.Z` on
`main` is the release; CI publishes the conan package for every platform
lane under one version, `<tag>.<build number>`. So "bump the version" is
"move the tag", never an edit. Additive headers are a minor bump; a
behaviour change to something already published is not. The release's
paragraph goes at the top of the release notes below.

All lanes must export byte-identical recipe files, or the release splits
across two conan recipe revisions and consumers — which resolve the latest
revision — see only half the binaries. `.gitattributes` pins LF for that
reason, and nothing machine-specific is exported (`_bdudata`, where the
build-number stamp lives, is not in the recipe's exports).

Runtime dependencies are pinned exactly, with no ranges: the shipped binary
embeds the versions it was built against, and a consumer whose cache
resolves a range differently computes a different package_id and finds no
binary. Bump the pins deliberately and republish.

The macOS and Windows packages are built on real machines rather than by
cross-compiling, because conan labels a cross-built binary with the
*container* toolchain's settings: a real Mac or Windows consumer computes a
different package_id, matches nothing, and falls into the recipe's
source-build refusal.

`tools/crossbuild.sh` runs a cross lane against the working tree on a box
with nested docker, in the same image and with the same buildutil command
line as CI. Each lane's image is a required environment variable with no
default:

```
OXBOX_OSXCROSS_IMAGE=<osxcross image>  tools/crossbuild.sh macos
OXBOX_MSVC_IMAGE=<msvc-wine image>     tools/crossbuild.sh windows
```

`BUILDUTIL_WHEEL` or `BUILDUTIL_INDEX` says where the driver is installed
from inside the container. The script keeps `_conanhome-<lane>/` (the conan
cache) and `_crossbuild-<lane>/` (the container user's HOME, and the wine
prefix) between runs, so the second attempt is fast. Both lanes build with
`--skip-dependency-upload-so-everyone-rebuilds-from-source` and put nothing
on a remote.

## Doctrine

What a consumer has to know about how this library behaves:

- **Parsing answers `std::optional` and never throws.** Trailing junk is a
  rejection, not a prefix match; overflow is a rejection, not a wrap; empty
  input is absent, not zero. A caller that wants an exception wraps it.
- **Formatting cannot fail**, so it returns. A bounded formatter says what
  it managed by the size of what it answers with.
- **Bounded reading of bytes that arrived from somewhere latches.**
  `Sound()` is checked once at the end, not a throw per field.
- **`std::expected` is not used anywhere in this library.**
- Exceptions come from `utilities/errors.hpp`'s family, except `platform`,
  which throws `std::runtime_error` naming the module, the call, the path
  and what the OS said.
- **Per-host code splits by buildutil's platform tags** (`*.posix.cpp`,
  `*.win32.cpp`) and never by an `#ifdef`. Tests split the same way, down to
  a `.linux` tag on the cases that need `/dev/full` and `RLIMIT_AS`.

## Release notes

**0.33.1 — documentation.** A known-issues paragraph no longer names an
internal image; no code change.

**0.33.0 — one exception template.** `utilities/exception.hpp` adds
`Exception<ID, Base, FORMAT, Args...>`: an exception type is a `using`
alias over it, its format checked against its arguments at compile time.
Additive: nothing already published moved.

**0.31.0 — shared joining and growing byte output.** `Joined` joins char
ranges or formatted values, optionally through a projection. `GrowingWriter`
appends typed scalars through `Store` (little-endian by default), zeros,
bytes and raw narrow/UTF-16 text. `Release()` moves out the complete vector
and leaves it empty, including when supplied by the caller. UTF-16 text
uses the writer’s byte order; views returned by `Bytes()` are invalidated by
reallocation or release.

**0.29.0 — a fourth contract mode, `THROW`.** A broken contract can now be
caught: `ContractMode::THROW` says the failure line as every other mode
does and then throws `ContractFailure`, a `std::runtime_error` whose
`what()` is that line and which carries the kind, the text and the
`std::source_location` on their own. `Unreachable` throws there too and
stays `[[noreturn]]`; `NotImplemented` throws rather than returning. A host
that catches it can halt on a screen with the whole failure in it.
Additive: nothing already published moved, and the three older modes are
unchanged.

**0.28.2 — a joined rest collector folds.** `cli/rest-collector.hpp`
joined the tail with `std::views::join_with`, which the libc++ Apple's
SDK ships (20.1) does not have, so an osxcross build of any consumer
with a CLI failed; it is a `std::ranges::fold_left` now. No behaviour
change.

**0.28.1 — licensed.** MIT licence, recipe metadata, example hosts in the
tests; no API change.

**0.28.0 — contracts stop, complain or ignore.** `Contracts<ENABLED>` is
`Contracts<MODE>`, and `ContractMode` names the three: a broken contract
ends the program, says its line and carries on, or costs nothing — a game's
betas run past a hole with the console saying what was hit, and its tests
stop on it. `Unreachable` stops in `COMPLAIN` too, a closed switch's
default having no continuation; `NotImplemented` returns in `COMPLAIN` and
in `IGNORE` and is no longer `[[noreturn]]`, which every caller of it has to
read.
Breaking: the template argument is a `ContractMode`, not a `bool`, and a
consumer's binding moves with it.

**0.27.0 — the contract facility.** `platform/contract.hpp` gives every
consumer `Expects`, `Ensures`, `Unreachable(value)` and `NotImplemented`
through `Contracts<ENABLED>`, whose switch is the project's own buildutil
option rather than `NDEBUG`: the checks run in release too, and switched
off they cost nothing. A failure says the kind, the file, the line, the
function and the text on one line and ends the program — to stderr on a
host, to the browser console in the wasm lane, split by tag like the rest
of the module. Additive: nothing already published moved.

**0.26.4 — the platform module's posix files build in the browser.** No
source change: republished with buildutil 0.78.2 baked in, where
Emscripten is a member of the `posix` family, so `mapped-file.posix.cpp`,
`native-file.posix.cpp`, `memory.posix.cpp` and `console.posix.cpp`
compile for wasm as they compile for Linux. `http` stays native.

**0.26.3 — Boost and OpenSSL are native requirements.** They are http's
alone, so the recipe requires them on Linux, macOS and Windows only;
on Emscripten the graph has neither and Conan no longer refuses the
package for a requirement no component uses.

**0.26.2 — http is a native module**. Emscripten builds omit `http`, so
browser consumers can build the other modules from source. Including an
HTTP header that reaches `asio.hpp` there gives the module's own refusal.

**0.26.1 — the build driver baked into every publish.** `buildutil publish
--bake-buildutil` on every lane, so a consumer with no prebuilt binary for
its platform (the emscripten lane, wasm) builds oxbox from source in its
own conan cache instead of meeting the recipe's refusal. No code change.

**0.26.0 — cli short options; byte order marks read on request.** A
switch tagged `_Meta("-v")` is also spelled `-v` (`-o value`, `-o=value`,
no bundling, `-h, --help` unless a member claims `-h`; duplicate or
malformed tags fail to compile). `ByteOrder::READ_MARK` asks
`SniffByteOrderMark` and `ChunkDecoder` to read the encoding's own mark,
across feeds if need be; a stated order wins and keeps a leading U+FEFF
as text; the default order stays native.

**0.25.1 — cli help and diagnostics; a path member deserializes.**
Value options show a placeholder from their type (`--url <string>`,
`--port <int>`), a type mismatch names the rejected text, a subcommand
without a description keeps its help row, and help comments re-flow as
paragraphs with a blank line as the break. A
`std::filesystem::path` member now deserializes.

**0.25.0 — `serialization` grew a positional binary format.**
`PositionalFormat` (`positional`, `.bsp`) writes fields in scheme order
without names, using the same checked primitives as `BinaryFormat`.
Absent optionals occupy null slots. Writer and reader must agree on field
order; by-name wire queries and populated maps require a named format.
A consumer with a second path to OpenSSL pins `openssl` to the version
oxbox pins (3.6.3 today).

**0.24.0 — `http` grew a server.** Four new public headers
(`server-message.hpp`, `response-stream.hpp`, `router.hpp`, `server.hpp`)
and no change to anything already published. The server is plain TCP on the
executor its caller hands it, so it owns no thread and no io_context, and a
handler either returns a `ServerResponse` or writes a `ResponseStream` over
time. Nothing else in the module moved: the request and response types are
built out of the client half's `FieldTable`, `Method`, `MediaType` and
`Payload` rather than second copies of them.

**0.23.0 — new public headers, and one narrower include.** `cli`,
`utilities` and `serialization` each split a large header into one file per
responsibility. The original header stays as the entry point and includes
the pieces, so no existing include breaks and every name is still spelled
the way it was; the new headers are in the module tables above. What changes
for a consumer is what arrives for free: `serializable.hpp` no longer
reaches `utilities/unicode.hpp`, `utilities/span.hpp`, `utilities/bits.hpp`
or `utilities/short-types.hpp` on the way past, because it no longer
includes a header that needed them (`io.hpp`, which does use unicode, now
includes it itself). A consumer that was naming `Encoding`, `Bytes`, `U32`
or `AlignUp` after including only `serializable.hpp` has to include the
header that owns them. Nothing was removed and no signature moved.

**0.22.0 — `cli` became a compiled component** (`console.cpp` and its two
per-host halves), where it had been header-only. The recipe discovers
libraries by scanning what the mirror shipped, so nothing in `conanfile.py`
changed — which is exactly why it would go unnoticed. A consumer linking
`oxbox::cli` now links an archive, and so needs a published binary for its
target rather than just headers.

## Known issues

- A `std::filesystem::path` member does not deserialize.
  `read-walker.hpp:65` spells `PathFromString` unqualified, and neither
  ordinary lookup at the point of instantiation nor ADL on a `std::string`
  argument reaches `oxbox::utilities`. The write side works, so a round trip
  is what breaks.
- The cross-compiled Windows lane cannot run: buildutil's reflect generator
  finds no C++ standard library inside the msvc-wine container. The lane is
  allowed to fail for that reason, and `tools/negative-compile.sh`'s cl leg
  is the Windows coverage for anything header-only. Under
  `buildutil/msvc-wine:latest` that leg fails every compile with D8037, a
  fault of that image; an msvc-wine 18 image compiles. There
  the two number-text rows and the short-options bad-tag and bare-dash rows
  fail on cl, and the exception format rows' expected text is libstdc++'s,
  which cl's refusals do not carry.
- buildutil's reflect generator claims any header that spells
  `reflect_scheme` and emits its own, so a test fixture whose schemes are
  hand-written must be a `.inc`, not a `.hpp` (`cli/unit.test/*-cli.inc`).
- gcc with any of `-fsanitize=null`, `nonnull-attribute` or
  `returns-nonnull-attribute` (all part of `-fsanitize=undefined`) cannot
  constant-evaluate a format string viewed from a template parameter
  object, so under it an `Exception` alias can be neither constructed nor
  caught; clang is unaffected. The null check it trips is
  [GCC PR 71962](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=71962).

## License

MIT — see [LICENSE](LICENSE).
