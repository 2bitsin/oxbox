// NOLINTBEGIN(misc-non-private-member-variables-in-classes): the serialization DSL is a
// value/descriptor surface by design (module-wide ruling)
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iosfwd>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/format-json.hpp"
#include "oxbox/serialization/format-pack.hpp"
#include "oxbox/serialization/format-xml.hpp"
#include "oxbox/serialization/format-yaml.hpp"
#include "oxbox/serialization/serializable.hpp"
#include "oxbox/utilities/short-types.hpp"
#include "oxbox/utilities/span.hpp"
#include "oxbox/utilities/path.hpp"
#include "oxbox/utilities/unicode.hpp"

namespace oxbox::serialization
{

  // ── Built-in Sink / Source adapters ────────────────────────────────
  // All on the streaming contracts (docs/streaming-io.md §2): sources
  // fill the caller's buffer and advance it; sinks consume the caller's
  // span and advance it. None of these ever legitimately goes full.
  class StringSink {
  public:
    auto Write(std::span<std::byte const>& data) -> std::span<std::byte const> {
      auto const written{ data };
      _out.append_range(data | std::views::transform(
        [](std::byte b) { return static_cast<char>(b); }));
      data = data.subspan(data.size());
      return written;
    }
    [[nodiscard]] auto Out() const& -> std::string_view { return _out; }
    [[nodiscard]] auto Out() &&     -> std::string      { return std::move(_out); }
  private:
    std::string _out;
  };

  class ByteSink {
  public:
    auto Write(std::span<std::byte const>& data) -> std::span<std::byte const> {
      auto const written{ data };
      _out.append_range(data);
      data = data.subspan(data.size());
      return written;
    }
    [[nodiscard]] auto Out() const& -> oxbox::utilities::Bytes       { return _out; }
    [[nodiscard]] auto Out() &&     -> std::vector<std::byte> { return std::move(_out); }
  private:
    std::vector<std::byte> _out;
  };

  class ByteSpanSource {
  public:
    explicit ByteSpanSource(oxbox::utilities::Bytes bytes) : _rest{ bytes } {}
    auto Read(std::span<std::byte>& buffer) -> std::span<std::byte const> {
      auto const count{ std::min(buffer.size(), _rest.size()) };
      if (count == 0) return {};
      std::ranges::copy(_rest.first(count), buffer.begin());
      auto const filled{ std::span<std::byte const>{ buffer }.first(count) };
      buffer = buffer.subspan(count);
      _rest  = _rest.subspan(count);
      return filled;
    }
  private:
    oxbox::utilities::Bytes _rest;
  };

  // a string's bytes, viewed in place -- the streaming is ByteSpanSource's
  class StringSource : public ByteSpanSource {
  public:
    explicit StringSource(std::string_view text)
    : ByteSpanSource{ std::as_bytes(std::span{ text.data(), text.size() }) } {}
  };

  // ── TokenTraits / TokenStreamSource ────────────────────────────────
  // A string sequence re-joined into ONE byte stream, streaming
  // (docs/streaming-io.md §4). The invariant is the TOKEN LIST, not the
  // exact text: a separator goes between elements, an element containing
  // any separator (or nothing at all) is quoted whole, and quote/escape
  // characters inside an element are escaped -- so re-splitting the
  // stream yields exactly the original elements, whatever they contain.
  // The traits are ALWAYS present (default initialized when not passed)
  // and are the sole source of truth -- the algorithm hardcodes nothing.
  // `separator` is the separator SET: its FIRST character joins, ANY of
  // them forces quotes; the default is the whitespace class, matching a
  // whitespace-splitting lexer. An EMPTY set turns the machinery off --
  // no separators, no quoting, no escaping: fragments of format text
  // concatenate as-is.
  namespace detail
  {
    template <Character C>
    inline constexpr std::array<C, 4> k_whitespace{
      static_cast<C>(' '),  static_cast<C>('\t'),
      static_cast<C>('\n'), static_cast<C>('\r') };
  }

  template <Character C>
  struct TokenTraits {
    C left_quote { static_cast<C>('"')  };
    C right_quote{ static_cast<C>('"')  };
    std::basic_string_view<C> separator{
      detail::k_whitespace<C>.data(), detail::k_whitespace<C>.size() };
    C escape     { static_cast<C>('\\') };
  };

  // The streaming adapter: a byte Source (docs/streaming-io.md §2.1) over
  // any string sequence. State is O(1) -- the sequence iterator, the
  // position inside the current element, the UTF decode state and a few
  // encoded bytes -- so an arbitrarily long (or non-terminating) sequence
  // streams; how much to materialize is the CONSUMER's decision. Elements
  // a range yields as lvalues are viewed in place; prvalue temporaries
  // are adopted by move (the element must exist somewhere -- the adapter
  // just refuses to be a second copy).
  template <typename R, Character C = SequenceCharT<R>>
    requires StringSequenceOf<R, C>
  class TokenStreamSource
  {
  public:
    explicit TokenStreamSource(R&& range, TokenTraits<C> traits = {})
    : _range{ std::forward<R>(range) }
    , _traits{ traits }
    , _it { std::ranges::begin(_range) }
    , _end{ std::ranges::end(_range) }
    {}

    // The §2.1 contract. Fills a prefix of `buffer`, advances `buffer`
    // past it, returns the filled slice; empty return = end of stream,
    // idempotently. Precondition: !buffer.empty().
    auto Read(std::span<std::byte>& buffer) -> std::span<std::byte const>
    {
      auto const out{ buffer };
      std::size_t count{ 0 };
      while (count < out.size()) {
        if (_carry_at < _carry_len) {
          auto const pending{ std::as_bytes(std::span{ _carry })
                                .subspan(_carry_at, _carry_len - _carry_at) };
          auto const take{ std::min(pending.size(), out.size() - count) };
          std::ranges::copy(pending.first(take), out.subspan(count).begin());
          count     += take;
          _carry_at += static_cast<std::uint8_t>(take);
          continue;
        }
        if (auto const unit{ _NextUnit() }) {
          using namespace oxbox::utilities;
          if (auto const cp{ UtfDecode<char32_t>(_decode, *unit) }; cp)
            { _PushCp(*cp); }
          continue;
        }
        if (_carry_at < _carry_len) continue;   // end-of-element flush landed
        break;                                  // genuinely done
      }
      if (count == 0) return {};
      buffer = buffer.subspan(count);
      return std::span<std::byte const>{ out }.first(count);
    }

  private:
    using _Ref = std::ranges::range_reference_t<R>;
    static constexpr bool k_adopts{ !std::is_lvalue_reference_v<_Ref> };
    struct _NoAdopt {};
    using _Adopted = std::conditional_t<
      k_adopts, std::optional<std::remove_cvref_t<_Ref>>, _NoAdopt>;

    enum class _Phase : std::uint8_t { FETCH, SEPARATOR, OPEN, CONTENT, CLOSE };

    // what must carry the escape prefix inside an element
    [[nodiscard]] auto _NeedsEscape(C unit) const -> bool
    {
      return !_traits.separator.empty()
          && (unit == _traits.left_quote || unit == _traits.right_quote
              || unit == _traits.escape);
    }

    auto _LoadElement() -> void
    {
      if constexpr (k_adopts) {
        _adopted.emplace(*_it);
        _content = std::basic_string_view<C>{ *_adopted };
      } else {
        _content = std::basic_string_view<C>{ *_it };
      }
      _wrap = !_traits.separator.empty()
           && (_content.empty()
               || _content.find_first_of(_traits.separator)
                    != std::basic_string_view<C>::npos);
    }

    // a truncated multi-unit sequence at a boundary becomes U+FFFD, so a
    // following separator or quote unit is never eaten by the pending state
    auto _FlushDecode() -> void
    {
      if (_decode.step == 0) return;
      _decode = {};
      _PushCp(char32_t{ 0xFFFD });
    }

    auto _PushCp(char32_t cp) -> void
    {
      using oxbox::utilities::UtfEncode;
      if (_carry_at == _carry_len) { 
        _carry_at = _carry_len = 0; }
      
      namespace stdr = std::ranges; 
      namespace stdv = stdr::views;
      auto const [count, words] = UtfEncode<char8_t>(cp);
      auto const dest{ stdr::begin(_carry) + _carry_len };
      stdr::copy(words | stdv::take(count), dest);
      _carry_len += count;
    }

    // the next unit of the joined stream: separator, quotes, escapes and
    // element characters in emission order; nullopt once the sequence ends
    auto _NextUnit() -> std::optional<C>
    {
      for (;;) {
        switch (_phase) {
        case _Phase::FETCH:
          if (_it == _end) { _FlushDecode(); return std::nullopt; }
          _LoadElement();
          _phase = std::exchange(_first, false) ? _Phase::OPEN
                                                : _Phase::SEPARATOR;
          break;
        case _Phase::SEPARATOR:
          _phase = _Phase::OPEN;
          if (!_traits.separator.empty()) return _traits.separator.front();
          break;
        case _Phase::OPEN:
          _phase = _Phase::CONTENT;
          if (_wrap) return _traits.left_quote;
          break;
        case _Phase::CONTENT: {
          if (_content.empty()) {
            _FlushDecode();
            _phase = _Phase::CLOSE;
            break;
          }
          C const unit{ _content.front() };
          if (!_escaped && _NeedsEscape(unit)) {
            _FlushDecode();
            _escaped = true;
            return _traits.escape;
          }
          _escaped = false;
          _content.remove_prefix(1);
          return unit;
        }
        case _Phase::CLOSE:
          ++_it;
          _phase = _Phase::FETCH;
          if (_wrap) return _traits.right_quote;
          break;
        }
      }
    }

    R                             _range;
    TokenTraits<C>                _traits;
    std::ranges::iterator_t<R>    _it;
    std::ranges::sentinel_t<R>    _end;
    std::basic_string_view<C>     _content{};
    [[no_unique_address]] _Adopted _adopted{};
    _Phase                        _phase{ _Phase::FETCH };
    bool                          _first{ true };
    bool                          _wrap{ false };
    bool                          _escaped{ false };
    oxbox::utilities::UtfDecodeState     _decode{};
    std::array<char8_t, 8>        _carry{};
    std::uint8_t                  _carry_len{ 0 };
    std::uint8_t                  _carry_at{ 0 };
  };

  template <typename R, Character C>
  TokenStreamSource(R&&, TokenTraits<C>) -> TokenStreamSource<R, C>;
  template <typename R>
  TokenStreamSource(R&&) -> TokenStreamSource<R>;

  // no slurp: chunks are pulled from the stream as the consumer asks
  class IstreamSource {
  public:
    explicit IstreamSource(std::istream& in) : _in{ in } {}
    auto Read(std::span<std::byte>& buffer) -> std::span<std::byte const> {
      _in.read(std::bit_cast<char*>(buffer.data()),
               static_cast<std::streamsize>(buffer.size()));
      if (_in.bad()) throw IoError{ "input stream failed mid-read" };
      auto const count{ static_cast<std::size_t>(_in.gcount()) };
      if (count == 0) return {};
      auto const filled{ std::span<std::byte const>{ buffer }.first(count) };
      buffer = buffer.subspan(count);
      return filled;
    }
  private:
    std::istream& _in;   // NOLINT(*-ref-data-members): non-owning by design
  };

  class OstreamSink {
  public:
    explicit OstreamSink(std::ostream& out) : _out{ out } {}
    auto Write(std::span<std::byte const>& data) -> std::span<std::byte const> {
      auto const written{ data };
      _out.write(std::bit_cast<char const*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
      if (!_out) throw IoError{ "output stream failed mid-write" };
      data = data.subspan(data.size());
      return written;
    }
  private:
    std::ostream& _out;   // NOLINT(*-ref-data-members): non-owning by design
  };


  // ── String-form Serialize / Deserialize ────────────────────────────
  template <typename F, typename T>
  auto Serialize(T& obj) -> std::string {
    StringSink sink;
    Serialize<F>(obj, sink);
    return std::move(sink).Out();
  }

  template <typename F, typename T>
    requires (!HasArchiveHook<T>)
  auto Serialize(T const& obj) -> std::string {
    StringSink sink;
    Serialize<F>(obj, sink);
    return std::move(sink).Out();
  }

  template <typename F, typename T>
  auto Deserialize(std::string_view input) -> T {
    StringSource src{input};
    return Deserialize<F, T>(src);
  }


  // ── Byte-blob Serialize / Deserialize (for binary formats) ──────────
  template <typename F, typename T>
    requires (!HasArchiveHook<T>)
  auto SerializeBytes(T const& obj) -> std::vector<std::byte> {
    ByteSink sink;
    Serialize<F>(obj, sink);
    return std::move(sink).Out();
  }

  template <typename F, typename T>
  auto DeserializeBytes(oxbox::utilities::Bytes input) -> T {
    ByteSpanSource src{input};
    return Deserialize<F, T>(src);
  }


// 
  template <typename E>
    requires HasEnumMap<E>
  [[nodiscard]] auto FromString(std::string_view s) -> std::optional<E> {
    return EnumMapFor<E>().FromString(s);
  }


  // ── Stream-form Serialize / Deserialize ────────────────────────────
  template <typename T, typename F>
  auto DeserializeFrom(std::istream& in, F /*format*/ = {}) -> T {
    IstreamSource src{in};
    return Deserialize<F, T>(src);
  }

  template <typename F, typename T>
  auto SerializeTo(T& obj, std::ostream& out, F /*format*/ = {}) -> void {
    OstreamSink sink{out};
    Serialize<F>(obj, sink);
  }

  template <typename F, typename T>
    requires (!HasArchiveHook<T>)
  auto SerializeTo(T const& obj, std::ostream& out, F /*format*/ = {}) -> void {
    OstreamSink sink{out};
    Serialize<F>(obj, sink);
  }


  // ── Format-specific convenience shims ──────────────────────────────
  template <Sink S, typename T>
  auto ToJson(T& obj, S& sink) -> void { Serialize<JsonFormat>(obj, sink); }

  template <Sink S, typename T>
    requires (!HasArchiveHook<T>)
  auto ToJson(T const& obj, S& sink) -> void { Serialize<JsonFormat>(obj, sink); }

  template <typename T>
  auto ToJson(T& obj) -> std::string { return Serialize<JsonFormat>(obj); }

  template <typename T>
    requires (!HasArchiveHook<T>)
  auto ToJson(T const& obj) -> std::string { return Serialize<JsonFormat>(obj); }

  template <typename T, Source Src>
  auto FromJson(Src& source) -> T { return Deserialize<JsonFormat, T>(source); }

  template <typename T>
  auto FromJson(std::string_view input) -> T {
    return Deserialize<JsonFormat, T>(input);
  }

  template <Sink S, typename T>
  auto ToYaml(T& obj, S& sink) -> void { Serialize<YamlFormat>(obj, sink); }

  template <Sink S, typename T>
    requires (!HasArchiveHook<T>)
  auto ToYaml(T const& obj, S& sink) -> void { Serialize<YamlFormat>(obj, sink); }

  template <typename T>
  auto ToYaml(T& obj) -> std::string { return Serialize<YamlFormat>(obj); }

  template <typename T>
    requires (!HasArchiveHook<T>)
  auto ToYaml(T const& obj) -> std::string { return Serialize<YamlFormat>(obj); }

  template <typename T, Source Src>
  auto FromYaml(Src& source) -> T { return Deserialize<YamlFormat, T>(source); }

  template <typename T>
  auto FromYaml(std::string_view input) -> T {
    return Deserialize<YamlFormat, T>(input);
  }

  template <Sink S, typename T>
  auto ToXml(T& obj, S& sink) -> void { Serialize<XmlFormat>(obj, sink); }

  template <Sink S, typename T>
    requires (!HasArchiveHook<T>)
  auto ToXml(T const& obj, S& sink) -> void { Serialize<XmlFormat>(obj, sink); }

  template <typename T>
  auto ToXml(T& obj) -> std::string { return Serialize<XmlFormat>(obj); }

  template <typename T>
    requires (!HasArchiveHook<T>)
  auto ToXml(T const& obj) -> std::string { return Serialize<XmlFormat>(obj); }

  template <typename T, Source Src>
  auto FromXml(Src& source) -> T { return Deserialize<XmlFormat, T>(source); }

  template <typename T>
  auto FromXml(std::string_view input) -> T {
    return Deserialize<XmlFormat, T>(input);
  }

// 

  namespace _detail
  {
    inline auto _ExtensionOf(std::filesystem::path const& path)
      -> std::string
    { using namespace oxbox::utilities;
      return std::string{ PathToString(path.extension()) };
    }

    [[noreturn]] inline auto _ThrowUnknownExt(std::string const& ext) -> void {
      throw oxbox::serialization::ParseError{
        "unknown serialization format for extension: " + ext};
    }

// 
    template <typename F>
    auto _OpenIn(std::filesystem::path const& path) -> std::ifstream {
      std::ifstream ifs{ path, std::ios::in | FormatTraits<F>::openmode };
      if (!ifs) throw oxbox::serialization::FileOpenError{path};
      return ifs;
    }

    template <typename F>
    auto _OpenOut(std::filesystem::path const& path) -> std::ofstream {
      std::ofstream ofs{ path, std::ios::out | FormatTraits<F>::openmode };
      if (!ofs) throw oxbox::serialization::FileOpenError{path};
      return ofs;
    }
  }

  // Explicit-format file I/O — the format picks the stream mode (text/binary).
  template <typename T, typename F>
  auto DeserializeFrom(std::filesystem::path const& path) -> T {
    auto ifs = _detail::_OpenIn<F>(path);
    return DeserializeFrom<T>(ifs, F{});
  }

  template <typename F, typename T>
  auto SerializeTo(T& obj, std::filesystem::path const& path) -> void {
    auto ofs = _detail::_OpenOut<F>(path);
    SerializeTo<F>(obj, ofs);
  }

  template <typename F, typename T>
    requires (!HasArchiveHook<T>)
  auto SerializeTo(T const& obj, std::filesystem::path const& path) -> void {
    auto ofs = _detail::_OpenOut<F>(path);
    SerializeTo<F>(obj, ofs);
  }

  // ── string-sequence forms ──────────────────────────────────────────
  // A string sequence streams into any character format through
  // TokenStreamSource (docs/streaming-io.md §4.3). The format is now
  // explicit: it used to default to CliFormat, and command-line parsing
  // has moved to the oxbox::cli module. Traits are always present --
  // default initialized when not passed -- and are the source of truth:
  // pass your own characters to change the joining, or an empty
  // separator set to concatenate fragments of format text as-is.
  template <typename T, typename F, StringSequence R,
            Character C = SequenceCharT<R>>
  auto DeserializeFrom(
      R&& args,
      std::type_identity_t<TokenTraits<C>> traits = {}) -> T {
    TokenStreamSource src{ std::forward<R>(args), traits };
    return Deserialize<F, T>(src);
  }

  // The main()-shaped form; argv[0] is the program name and is skipped.
  template <typename T, typename F, Character C>
  auto DeserializeFrom(int argc, C const* const* argv,
                       std::type_identity_t<TokenTraits<C>> traits = {}) -> T {
    auto const count{ argc > 0 ? static_cast<std::size_t>(argc) - 1 : 0 };
    return DeserializeFrom<T, F>(
      std::span{ argv + (argc > 0 ? 1 : 0), count }, traits);
  }

  // Format chosen from the path's extension via FormatTraits (.json/.yaml/.xml/.bsx).
  template <typename T>
  auto DeserializeFrom(std::filesystem::path const& path) -> T {
    auto const ext = _detail::_ExtensionOf(path);
    auto const fmt = FormatFromExtension(ext);
    if (!fmt) _detail::_ThrowUnknownExt(ext);
    return std::visit([&]<typename F>(F) -> T { return DeserializeFrom<T, F>(path); }, *fmt);
  }

  template <typename T>
  auto SerializeTo(T& obj, std::filesystem::path const& path) -> void {
    auto const ext = _detail::_ExtensionOf(path);
    auto const fmt = FormatFromExtension(ext);
    if (!fmt) _detail::_ThrowUnknownExt(ext);
    std::visit([&]<typename F>(F) { SerializeTo<F>(obj, path); }, *fmt);
  }

  template <typename T>
    requires (!HasArchiveHook<T>)
  auto SerializeTo(T const& obj, std::filesystem::path const& path) -> void {
    auto const ext = _detail::_ExtensionOf(path);
    auto const fmt = FormatFromExtension(ext);
    if (!fmt) _detail::_ThrowUnknownExt(ext);
    std::visit([&]<typename F>(F) { SerializeTo<F>(obj, path); }, *fmt);
  }
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
