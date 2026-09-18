#pragma once

#include <cstddef>
#include <format>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "oxbox/serialization/errors.hpp"
#include "oxbox/utilities/short-types.hpp"
#include "oxbox/utilities/span.hpp"

namespace oxbox::serialization::detail::binary_wire
{
  struct BinaryWire
  {
    using U08 = oxbox::utilities::U08;
    using U32 = oxbox::utilities::U32;
    using U64 = oxbox::utilities::U64;
    using Bytes = oxbox::utilities::Bytes;
    using Blob = std::vector<std::byte>;

    enum Tag : U08 {
      TAG_NULL,
      TAG_BOOL,
      TAG_INT,
      TAG_DOUBLE,
      TAG_STRING,
      TAG_OBJECT,
      TAG_ARRAY
    };

    // Sink/Source carries chars even when the wire is a non-terminated byte blob.
    static auto AsChars(Bytes bytes) -> std::string_view
    {
      auto const chars{ oxbox::utilities::SpanCast<char const>(bytes) };
      return { chars.data(), chars.size() };
    }
    static auto Append(Blob& out, Bytes bytes) -> void
    {
      out.insert(out.end(), bytes.begin(), bytes.end());
    }

    static constexpr U32 VARINT_VALUE_BITS{ 64u };
    static constexpr U32 VARINT_BITS_PER_BYTE{ 7u };
    static constexpr U08 VARINT_VALUE_MASK{ 0x7Fu };
    static constexpr U08 VARINT_CONTINUES{ 0x80u };

    static auto EncodeVarInt(Blob& out, U64 value) -> void
    {
      do {
        auto byte{ static_cast<U08>(value & VARINT_VALUE_MASK) };
        value >>= VARINT_BITS_PER_BYTE;
        if (value != 0u)
          byte |= VARINT_CONTINUES;
        out.push_back(std::byte{ byte });
      } while (value != 0u);
    }

    static auto WriteInt(Blob& out, U64 value) -> void
    {
      out.push_back(std::byte{ TAG_INT });
      EncodeVarInt(out, value);
    }

    class Cursor;

    static auto DecodeVarInt(Bytes data, std::size_t& pos) -> U64;

    static auto ReadInt(Bytes data, std::size_t& pos) -> U64;

    static auto NodeLength(Bytes data, std::size_t at) -> std::size_t;
  };

  // Save states are untrusted: every wire access must be bounded and malformed input must raise ParseError.
  class BinaryWire::Cursor
  {
  public:
    struct Payload
    {
      std::size_t at;
      Bytes bytes;
    };

    explicit constexpr Cursor(Bytes data) noexcept : _data{ data } {}

    [[nodiscard]] auto Slice(std::size_t at, U64 count) const -> Bytes
    {
      if (count > _data.size() || at > _data.size() - count)
        ThrowOverrun(at, count);
      return _data.subspan(at, static_cast<std::size_t>(count));
    }

    [[nodiscard]] auto Byte(std::size_t at) const -> U08
    {
      return std::to_integer<U08>(Slice(at, 1u).front());
    }

    [[nodiscard]] auto TagAt(std::size_t at) const -> Tag
    {
      auto const raw{ Byte(at) };
      switch (static_cast<Tag>(raw)) {
      case TAG_NULL:
      case TAG_BOOL:
      case TAG_INT:
      case TAG_DOUBLE:
      case TAG_STRING:
      case TAG_OBJECT:
      case TAG_ARRAY:
        return static_cast<Tag>(raw);
      }
      throw ParseError{ std::format("binary: unknown node tag {} at offset {}",
                                    raw, at) };
    }

    // Protobuf's 64-bit LEB128 rule permits at most ten bytes; the tenth must be 0x00 or 0x01, never truncated surplus bits.
    auto VarInt(std::size_t& pos) const -> U64
    {
      U64 value{ 0u };
      for (U32 shift{ 0u }; shift < VARINT_VALUE_BITS;
           shift += VARINT_BITS_PER_BYTE) {
        auto const at{ pos++ };
        auto const byte{ Byte(at) };
        auto const bits{ static_cast<U64>(byte & VARINT_VALUE_MASK) };
        if (auto const room{ VARINT_VALUE_BITS - shift };
            room < VARINT_BITS_PER_BYTE && bits >> room != 0u)
          throw ParseError{ std::format(
              "binary: varint at offset {} carries more than {} bits", at,
              VARINT_VALUE_BITS) };
        value |= bits << shift;
        if ((byte & VARINT_CONTINUES) == 0u)
          return value;
      }
      throw ParseError{ std::format(
          "binary: unterminated varint ending at offset {}", pos) };
    }

    // String, object and array lengths are whole TAG_INT nodes: a tag byte followed by a varint.
    auto Int(std::size_t& pos) const -> U64
    {
      if (TagAt(pos) != TAG_INT)
        throw ParseError{ std::format(
            "binary: expected an integer node at offset {}", pos) };
      ++pos;
      return VarInt(pos);
    }

    [[nodiscard]] auto PayloadOf(std::size_t at) const -> Payload
    {
      std::size_t pos{ at + 1u };
      auto const length{ Int(pos) };
      return { pos, Slice(pos, length) };
    }

    [[nodiscard]] auto NodeLength(std::size_t at) const -> std::size_t
    {
      auto const tag{ TagAt(at) };
      switch (tag) {
      case TAG_NULL:
        return Extent(at, 1u);
      case TAG_BOOL:
        return Extent(at, 2u);
      case TAG_DOUBLE:
        return Extent(at, 1u + sizeof(U64));
      case TAG_INT: {
        std::size_t p{ at };
        static_cast<void>(Int(p));
        return p - at;
      }
      case TAG_STRING:
      case TAG_OBJECT:
      case TAG_ARRAY: {
        auto const payload{ PayloadOf(at) };
        return (payload.at - at) + payload.bytes.size();
      }
      }
      throw ParseError{ std::format(
          "binary: node tag {} at offset {} has no length rule",
          std::to_underlying(tag), at) };
    }

    // A child must fit its container before the reader can enter it.
    [[nodiscard]] auto NextNode(std::size_t at, std::size_t limit) const
        -> std::size_t
    {
      if (at >= limit)
        throw ParseError{ std::format(
            "binary: container ending at offset {} is truncated", limit) };
      auto const length{ NodeLength(at) };
      if (length > limit - at)
        throw ParseError{ std::format("binary: node at offset {} escapes its "
                                      "container ending at offset {}",
                                      at, limit) };
      return at + length;
    }

  private:
    [[nodiscard]] auto Extent(std::size_t at, std::size_t length) const
        -> std::size_t
    {
      static_cast<void>(Slice(at, length));
      return length;
    }

    [[noreturn]] auto ThrowOverrun(std::size_t at, U64 count) const -> void
    {
      throw ParseError{ std::format(
          "binary: read of {} byte(s) at offset {} runs past the {}-byte blob",
          count, at, _data.size()) };
    }

    Bytes _data;
  };

  inline auto BinaryWire::DecodeVarInt(Bytes data, std::size_t& pos) -> U64
  {
    return Cursor{ data }.VarInt(pos);
  }

  inline auto BinaryWire::ReadInt(Bytes data, std::size_t& pos) -> U64
  {
    return Cursor{ data }.Int(pos);
  }

  inline auto BinaryWire::NodeLength(Bytes data, std::size_t at) -> std::size_t
  {
    return Cursor{ data }.NodeLength(at);
  }

} // namespace oxbox::serialization::detail::binary_wire
