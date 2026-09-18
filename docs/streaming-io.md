# Streaming I/O for serialization

The `Source`/`Sink` contracts the serialization module is built on. Header
comments cite this file by section number (`concepts.hpp`, `stream.hpp`,
`io.hpp`, `utilities/errors.hpp`), so the numbering is load-bearing.

## 1. The constraint

An interface decides whether a module can stream. A reader-side
`All() -> std::string_view` forces every format reader to parse a fully
materialized buffer, makes `IstreamSource` slurp its stream on
construction, and makes a token sequence unconsumable incrementally even in
principle. None of the formats need that: binary walks tagged
length-prefixed data with bounded lookahead, nlohmann has input
adapters and SAX, and yaml-cpp and pugixml accept incremental input.

So the contracts below are byte-at-a-time by construction, and nothing in
the module buffers raw wire text. Each format materializes its own model —
a DOM, a blob — and nothing more.

## 2. The byte contracts

Both directions have the same shape: the caller's span is in-out and gets
advanced; the return value is the slice actually transferred.

### 2.1 Source

```cpp
auto Read(std::span<std::byte>& buffer) -> std::span<std::byte const>;
```

- The source fills a prefix of `buffer`, **advances `buffer`** past the
  filled region (it now denotes the remaining free space), and returns the
  filled slice, aliasing the caller's storage.
- **End of stream:** `buffer` is not advanced and the returned span is
  empty. Every subsequent `Read` behaves the same — EOF is idempotent.
- **I/O error:** throws. `buffer` is not advanced.
- Short reads are legal: the returned slice may be smaller than `buffer`. A
  read blocks until at least one byte is available or the stream ends, so
  an empty return *means* end, never "try again".
- **Precondition:** `!buffer.empty()`. An empty input buffer would make the
  empty return ambiguous with EOF; passing one is a caller bug.

The advance-by-reference makes accumulation loops trivial — the free-space
span does the bookkeeping:

```cpp
std::array<std::byte, 64 * 1024> storage;
std::span<std::byte> free{ storage };
while (auto const got = src.Read(free); !got.empty())
  consume(got);
```

### 2.2 Sink

```cpp
auto Write(std::span<std::byte const>& data) -> std::span<std::byte const>;
```

- The sink consumes a prefix of `data`, **advances `data`** past the
  written region, and returns the written slice.
- **Sink full** — the write-side mirror of EOF, a fixed-capacity sink out
  of room: `data` is not advanced and the returned span is empty,
  idempotently. The caller decides whether that is an error ("the wire did
  not fit") or a flush-and-continue point. An unbounded sink never
  legitimately returns empty.
- **I/O error:** throws. `data` is not advanced.
- Short writes are legal; a write blocks until at least one byte lands, or
  throws.
- **Precondition:** `!data.empty()` — the same ambiguity rule as reads.
- A drain loop must branch on the return. `while (!pending.empty())
  sink.Write(pending);` spins forever on a full sink.

### 2.3 Open

- **Zero-copy relaxation for in-memory sources.** Filling the caller's
  buffer costs a memcpy that a whole-buffer `All()` avoided. The known
  relaxation — a source may return a view into its own storage, valid until
  the next `Read`, instead of filling `buffer` — restores zero-copy for
  `StringSource` at the price of a subtler lifetime rule. The contract
  above stands until the owner rules otherwise.
- **Sink flush.** A source announces its own end with an empty return; a
  buffered sink has to be told. RAII (flush in the destructor, with the
  usual throwing-destructor caveats) or an optional `Flush()` capability
  writers probe. Undecided.

## 3. The text layer

The wire is bytes; text formats lex characters. `utilities/unicode.hpp` and
the headers it includes provide the pieces at the right granularity:

- `UtfDecode(UtfDecodeState&, unit) -> std::optional<char32_t>`
  (`utf-decode.hpp`) — the unit-at-a-time state machine, for units of any
  size. A code point straddling a chunk boundary lives in the state between
  `Read` calls, not in a buffer. This is the chunk-boundary-safe primitive.
- `BufferDecodeIterator` (`buffer-decode-iterator.hpp`) — lazy zero-copy
  code-point iteration within one contiguous chunk.
- `UtfEncode` (`utf-encode.hpp`) — a code point into units of a named
  width, at most four.
- `ChunkDecoder` (`transcode.hpp`) — the bounded-carry tier over those,
  beside `SniffByteOrderMark`, `DecodeResilient` and `EncodeAppend`.

Consequence: a text format needs no take-N carry buffer, because the decode
state machine is its carry. The only genuine partial-unit buffer left is
binary's fixed-width reads for tags and varints. Same pattern either way:
carry the partial unit, never demand the whole buffer.

## 4. String-sequence input

### 4.1 The parameter struct

```cpp
template <Character C>            // char, wchar_t, char8_t, char16_t, char32_t
struct TokenTraits {
  C left_quote { '"'  };
  C right_quote{ '"'  };
  std::basic_string_view<C> separator{ /* the whitespace class */ };
  C escape     { '\\' };
};
```

The traits are always present — every taking site default-initializes them
when none are passed, and they are never nullable — and they are the sole
source of truth: the algorithm hardcodes nothing. `separator` is a
separator *set*:

- the set's **first** character goes between elements;
- an element **containing any character of the set**, and the **empty
  element**, is quoted whole;
- **quote and escape characters inside an element are escaped** with the
  escape character, wherever they stand.

The default set is the whitespace class `" \t\n\r"`, because a
whitespace-splitting lexer is the common consumer, and that knowledge
belongs in the traits data rather than in the algorithm. An **empty set
turns the machinery off**: no separators, no quoting, no escaping —
fragments of format text concatenate as-is.

With a non-empty set the invariant is the **token list**, not the exact
text: the joined stream, split again, yields exactly the original elements,
whatever they contain. Quoting therefore carries no meaning beyond
grouping, and a consuming lexer must agree.

### 4.2 The adapter

One adapter, a pure byte `Source` (§2.1) over any string sequence:

```cpp
template <StringSequenceOf<C> R, Character C>
class TokenStreamSource;   // Read(span<std::byte>&) -> span<std::byte const>
```

- **Accepted sequences:** any input range whose elements convert to
  `std::basic_string_view<C>` for one of the five character types —
  `C const*`, `std::basic_string<C>`, `std::basic_string_view<C>`,
  literals, generated or transformed ranges.
- **Emission:** per element — the separator (except before the first), then
  the element per §4.1 — transcoded unit by unit to **UTF-8 bytes**, the
  wire encoding regardless of `C`. A multi-unit sequence truncated at an
  element boundary flushes as U+FFFD, so it can never eat the following
  separator, quote or escape.
- **Streaming guarantee:** state is O(1) — the sequence iterator, a
  position within the current element, the decode carry, and a ≤4-byte
  encode carry (a code point's UTF-8 may not fit the free space left in one
  `Read`). The adapter never materializes the sequence and never copies an
  element: where a range yields lvalue references it holds a view, and
  where a range fabricates prvalue temporaries it adopts the temporary by
  move. How much to materialize is the consumer's decision.

### 4.3 Entry points

Both live in `io.hpp` beside the other `DeserializeFrom` forms. The format
is an explicit template argument — there is no default, since command-line
parsing moved to the `oxbox::cli` module:

```cpp
// generic sequence
template <typename T, typename F, StringSequence R,
          Character C = SequenceCharT<R>>
auto DeserializeFrom(R&& args, TokenTraits<C> traits = {}) -> T;

// main()-shaped: argv[0] skipped
template <typename T, typename F, Character C>
auto DeserializeFrom(int argc, C const* const* argv,
                     TokenTraits<C> traits = {}) -> T;
```

One default for both: default-initialized traits, so the full joining
algorithm. Sequence elements are pre-split atoms — a shell-split argv, a
vector of tokens — whose boundaries must survive re-lexing, and quoting and
escaping are what guarantee that. Feeding fragments of format text (JSON
pieces, say) is the explicit case for an empty separator set.

## 5. Where each format stands

Every adapter — `StringSource`, `IstreamSource`, `ByteSpanSource`,
`StringSink`, `OstreamSink`, `ByteSink`, `TokenStreamSource` — implements
the §2 contracts, and no reader holds raw wire text.

- **JSON, YAML, XML** build their DOM straight off the stream through
  `detail::SourceBuf`, a `std::streambuf` over a `Source` with one fixed
  window; the raw text is never held alongside the DOM.
- **Binary** still pulls the wire into a blob (`detail::DrainBytes`): the
  blob is the model its bounds-checked `Cursor` walks. A stream-walking
  Cursor with bounded lookahead is the one step not taken.
- **Writers** buffer their own model — DOM, emitter, pieces — and drain the
  finished wire through `detail::PutBytes`/`PutText`. A bounded sink that
  fills mid-drain is an `IoError`.

## 6. Decided defaults

| Decision | Ruling |
|---|---|
| Quoting on or off | the `separator` set decides: non-empty is the full algorithm, empty is raw concatenation. Traits are always present, never nullable |
| Quoting scope | an element containing any separator-set character, or nothing, is quoted whole. Quotes group, they never classify |
| Escaping | the `escape` member (default `\\`) keeps quote and escape characters literal, so every element is representable |
| Wire encoding | always UTF-8 bytes, whatever `C` the input uses |
| Empty buffer to `Read`/`Write` | a precondition violation, not a signal |
| Would-block | inexpressible by design: an empty return means end or full, and calls otherwise block |
| Infinite sequences | the adapter streams them; the consumer decides what to materialize |
