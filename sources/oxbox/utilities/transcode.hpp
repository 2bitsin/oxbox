#pragma once
// The stream tier over unicode.hpp's primitives: a byte order mark, damage
// that must not end the read, and chunk boundaries that cut sequences in
// half. Nothing here re-implements a decode.

#include "oxbox/utilities/serdes.hpp"
#include "oxbox/utilities/span.hpp"
#include "oxbox/utilities/unicode.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

namespace oxbox::utilities::detail::transcode
{
  namespace stdr = std::ranges;

  class ByteOrder
  {
  public:
    enum ReadMark { READ_MARK };

    constexpr ByteOrder(std::endian order = std::endian::native) noexcept
    : _order{ order } { }
    constexpr ByteOrder(ReadMark) noexcept : _read_mark{ true } { }

    constexpr operator std::endian() const noexcept { return _order; }
    constexpr auto ReadsMark() const noexcept -> bool { return _read_mark; }
    constexpr auto operator==(ByteOrder const&) const noexcept
      -> bool = default;
    constexpr auto operator==(std::endian order) const noexcept -> bool
    { return !_read_mark && _order == order; }

  private:
    std::endian _order{ std::endian::native };
    bool _read_mark{ false };
  };

  // Default byte order is native, not RFC 2781 section 4.3's big-endian.
  struct TextFormat
  {
    Encoding    encoding{ Encoding::UTF8      };
    ByteOrder   order   { std::endian::native };
  };

  struct DecodeReport
  {
    std::size_t codepoints  { 0u };
    std::size_t replacements{ 0u };
  };

  // UTF-8 stops at four octets, UTF-16 at two units, UCS4 is four wide.
  inline constexpr auto LONGEST_SEQUENCE_BYTES{ std::size_t{ 4u } };

  inline constexpr auto MARK_UTF8{ std::array{
    std::byte{ 0xEFu },
    std::byte{ 0xBBu },
    std::byte{ 0xBFu } } };
  inline constexpr auto MARK_UTF16_BIG{ std::array{
    std::byte{ 0xFEu },
    std::byte{ 0xFFu } } };
  inline constexpr auto MARK_UTF16_LITTLE{ std::array{
    std::byte{ 0xFFu },
    std::byte{ 0xFEu } } };
  inline constexpr auto MARK_UTF32_BIG{ std::array{
    std::byte{ 0x00u },
    std::byte{ 0x00u },
    std::byte{ 0xFEu },
    std::byte{ 0xFFu } } };
  inline constexpr auto MARK_UTF32_LITTLE{ std::array{
    std::byte{ 0xFFu },
    std::byte{ 0xFEu },
    std::byte{ 0x00u },
    std::byte{ 0x00u } } };

  template <typename _Sink>
  concept CodepointSink = requires (_Sink sink, char32_t codepoint)
  {
    { *sink = codepoint };
    { ++sink };
  };

  // The step a resync slides by: sliding a byte at a time would invent
  // sequences that were never on the wire.
  inline constexpr auto _UnitBytes(Encoding encoding) noexcept -> std::size_t
  {
    using enum Encoding;
    switch (encoding)
    {
    case UCS1 : return 1u;
    case UTF8 : return 1u;
    case UCS2 : return 2u;
    case UTF16: return 2u;
    case UCS4 : return 4u;
    }
    return 1u;
  }

  // A source shorter than the mark is left alone: a split mark is a
  // refill's problem, never a silently eaten prefix.
  template <std::size_t _Size>
  inline constexpr auto _TakeMark(Bytes& source,
                                  std::array<std::byte, _Size> const& mark)
    noexcept -> bool
  {
    if (!stdr::starts_with(source, mark)) { return false; }
    Advance(source, _Size);
    return true;
  }

  // Only the declared encoding's own mark is read, and only on request.
  // A stated order keeps a leading U+FEFF as text; short marks stay intact.
  inline constexpr auto SniffByteOrderMark(Bytes& source,
                                          TextFormat declared) noexcept
    -> TextFormat
  {
    if (!declared.order.ReadsMark()) { return declared; }
    declared.order = std::endian::native;
    using enum Encoding;
    switch (declared.encoding)
    {
    case UCS1:                              // raw octets have no mark to spell
      break;
    case UTF8:                              // UTF-8 has no byte order
      _TakeMark(source, MARK_UTF8);
      break;
    case UCS2:
    case UTF16:
      if (_TakeMark(source, MARK_UTF16_BIG))
        { declared.order = std::endian::big; }
      else if (_TakeMark(source, MARK_UTF16_LITTLE))
        { declared.order = std::endian::little; }
      break;
    case UCS4:
      if (_TakeMark(source, MARK_UTF32_BIG))
        { declared.order = std::endian::big; }
      else if (_TakeMark(source, MARK_UTF32_LITTLE))
        { declared.order = std::endian::little; }
      break;
    }
    return declared;
  }

  // True only for a tail that ran out: one that resolved, even to the
  // invalid sentinel, is not pending.
  template <std::integral _Unit>
  inline constexpr auto _PendingUnits(Bytes tail, std::endian order)
    noexcept -> bool
  {
    UtfDecodeState state{ };
    while (sizeof(_Unit) <= tail.size_bytes())
    {
      if (UtfDecode<char32_t>(state, Fetch<_Unit>(order, tail)))
        { return false; }
    }
    return (state.step != 0u) || !tail.empty();
  }

  // DecodeFromBytes answers nullopt both for damage and for "need more
  // bytes"; this is the only place that tells the two apart.
  inline constexpr auto _IsPendingPrefix(Bytes tail, TextFormat format)
    noexcept -> bool
  {
    using enum Encoding;
    switch (format.encoding)
    {
    case UCS1:                              // fixed width: the only way to
    case UCS2:                              // fail is to be short of one
    case UCS4:  return !tail.empty();       // whole unit
    case UTF8 : return _PendingUnits<std::uint8_t >(tail, format.order);
    case UTF16: return _PendingUnits<std::uint16_t>(tail, format.order);
    }
    return false;
  }

  // Damage becomes one U+FFFD and the walk slides one code unit; what is
  // left in `source` on return is a partial sequence, never damage.
  template <CodepointSink _Sink>
  inline auto DecodeResilient(Bytes& source, _Sink&& sink,
                              TextFormat format = { })
    -> DecodeReport
  {
    auto report{ DecodeReport{ } };
    while (!source.empty())
    {
      if (auto const code{ DecodeFromBytes<char32_t>(
                             source, format.encoding, format.order) })
      { *sink = *code; ++sink;
        ++report.codepoints;
        continue; }
      // the primitive refused and left the span alone
      if ((source.size_bytes() < LONGEST_SEQUENCE_BYTES)
        && _IsPendingPrefix(source, format)) { break; }
      *sink = REPLACEMENT_CODEPOINT<char32_t>; ++sink;
      ++report.codepoints;
      ++report.replacements;
      Advance(source, _UnitBytes(format.encoding));
    }
    return report;
  }

  // A partial sequence at end of stream becomes one U+FFFD, however many
  // bytes of it arrived.
  template <CodepointSink _Sink>
  inline auto FinishDecode(Bytes& source, _Sink&& sink) -> DecodeReport
  {
    if (source.empty()) { return { }; }
    source = Bytes{ };
    *sink = REPLACEMENT_CODEPOINT<char32_t>; ++sink;
    return { .codepoints = 1u, .replacements = 1u };
  }

  // A codepoint the encoding cannot spell writes nothing.
  template <std::output_iterator<std::byte> _Out>
  inline auto EncodeAppend(char32_t codepoint, _Out out,
                           TextFormat format = { }) -> _Out
  {
    auto       storage{ std::array<std::byte, LONGEST_SEQUENCE_BYTES>{ } };
    auto       room   { AsWritableBytes(storage) };
    auto const written{ EncodeIntoBytes(codepoint, room,
                                        format.encoding, format.order) };
    if (written < 1) { return out; }
    return stdr::copy(Bytes{ storage }.first(
                        static_cast<std::size_t>(written)), out).out;
  }

  // Carries an initial mark on request, then incomplete code sequences;
  // either needs at most four octets across feeds.
  class ChunkDecoder
  {
  public:
    constexpr ChunkDecoder()                          noexcept = default;
    constexpr explicit ChunkDecoder(TextFormat format) noexcept
    : _text_format{ format } { }
    // Bytes cut by the end of the chunk are carried, not replaced.
    template <CodepointSink _Sink>
    auto Consume(Bytes chunk, _Sink&& sink) -> DecodeReport
    {
      if (_text_format.order.ReadsMark() && !_ReadMark(chunk))
        { return { }; }
      auto report{ _ConsumeCarry(chunk, sink) };
      // a carry still standing means the seam swallowed the whole chunk, and
      // what it just wrote must not be overwritten
      if (_carry_length != 0u) { return report; }
      auto const body{ DecodeResilient(chunk, sink, _text_format) };
      report.codepoints   += body.codepoints;
      report.replacements += body.replacements;
      _Carry(chunk);
      return report;
    }
    template <CodepointSink _Sink>
    auto Finish(_Sink&& sink) -> DecodeReport
    {
      auto carried{ _CarriedBytes() };
      if (_text_format.order.ReadsMark())
        { _text_format = SniffByteOrderMark(carried, _text_format); }
      auto report{ DecodeResilient(carried, sink, _text_format) };
      auto const tail{ FinishDecode(carried, sink) };
      report.codepoints += tail.codepoints;
      report.replacements += tail.replacements;
      _carry_length = 0u;
      return report;
    }
    auto Pending() const noexcept -> std::size_t { return _carry_length; }
    auto Format () const noexcept -> TextFormat  { return _text_format;  }
  private:
    auto _CarriedBytes() const noexcept -> Bytes
    { return Bytes{ _carry_bytes }.first(_carry_length); }
    auto _ReadMark(Bytes& chunk) noexcept -> bool
    {
      while (!chunk.empty())
      {
        _carry_bytes[_carry_length++] = chunk.front();
        Advance(chunk, 1u);
        auto held{ _CarriedBytes() };
        auto const format{ SniffByteOrderMark(held, _text_format) };
        if (held.size() != _carry_length
          || _carry_length == LONGEST_SEQUENCE_BYTES)
        {
          _text_format = format;
          _carry_length = held.size();
          return true;
        }
      }
      return false;
    }
    auto _Carry(Bytes tail) noexcept -> void
    {
      _carry_length = tail.size_bytes();
      stdr::copy(tail, _carry_bytes.begin());
    }
    // Stitches the carry onto the chunk head, then hands the chunk back
    // advanced past the bytes the stitch consumed.
    template <CodepointSink _Sink>
    auto _ConsumeCarry(Bytes& chunk, _Sink&& sink) -> DecodeReport
    {
      if (_carry_length == 0u) { return { }; }
      auto const head{ std::min(chunk.size_bytes(), LONGEST_SEQUENCE_BYTES) };
      auto const carried{ _CarriedBytes() };
      auto joined{ std::array<std::byte, 2u * LONGEST_SEQUENCE_BYTES>{ } };
      auto const seam   { stdr::copy(carried, joined.begin()).out };
      stdr::copy(chunk.first(head), seam);
      auto const length{ _carry_length + head };
      auto       stitched{ Bytes{ joined }.first(length) };
      auto const report  { DecodeResilient(stitched, sink, _text_format) };
      auto const eaten   { length - stitched.size_bytes() };
      if (eaten < _carry_length)
      { // the whole chunk went into the stitch and is still incomplete
        _Carry(stitched);
        chunk = Bytes{ };
        return report; }
      _carry_length = 0u;
      Advance(chunk, eaten - carried.size_bytes());
      return report;
    }
    std::array<std::byte, LONGEST_SEQUENCE_BYTES>
                 _carry_bytes { };
    std::size_t  _carry_length{ 0u };
    TextFormat   _text_format { };
  };

}

namespace oxbox::utilities
{
  using detail::transcode::ByteOrder;
  using detail::transcode::ChunkDecoder;
  using detail::transcode::CodepointSink;
  using detail::transcode::DecodeReport;
  using detail::transcode::DecodeResilient;
  using detail::transcode::EncodeAppend;
  using detail::transcode::FinishDecode;
  using detail::transcode::SniffByteOrderMark;
  using detail::transcode::TextFormat;
}
