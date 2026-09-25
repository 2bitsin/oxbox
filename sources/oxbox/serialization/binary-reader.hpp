#pragma once

#include "oxbox/serialization/binary-wire.hpp"
#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/stream.hpp"
#include "oxbox/utilities/serdes.hpp"

#include <bit>
#include <cstdint>
#include <type_traits>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace oxbox::serialization::detail::binary_reader
{
  template <Source Src, bool NAMED>
  class BinaryReader : public binary_wire::BinaryWire
  {
  public:
    // The blob is the reader's model because Cursor validates every access against its full extent.
    explicit BinaryReader(Src& source) : _data{ detail::DrainBytes(source) } {}

    template <typename T> auto Read() -> T
    {
      auto const wire{ Wire() };
      auto const tag{ wire.TagAt(_cur) };
      if constexpr (std::same_as<T, bool>) {
        if (tag != TAG_BOOL)
          ThrowType("bool");
        return wire.Byte(_cur + 1u) != 0u;
      } else if constexpr (std::same_as<T, std::string>) {
        if (tag != TAG_STRING)
          ThrowType("string");
        return std::string{ AsChars(wire.PayloadOf(_cur).bytes) };
      } else if constexpr (std::same_as<T, std::int64_t>) {
        if (tag != TAG_INT)
          ThrowType("integer");
        std::size_t p{ _cur };
        return static_cast<std::int64_t>(wire.Int(p));
      } else if constexpr (std::same_as<T, std::uint64_t>) {
        if (tag != TAG_INT)
          ThrowType("integer");
        std::size_t p{ _cur };
        return wire.Int(p);
      } else if constexpr (std::same_as<T, double>) {
        if (tag != TAG_DOUBLE)
          ThrowType("number");
        return std::bit_cast<double>(oxbox::utilities::Fetch<U64>(
            wire.Slice(_cur + 1u, sizeof(U64)), oxbox::utilities::NoAdvance));
      } else {
        static_assert(sizeof(T) == 0,
                      "BinaryFormat::Reader::Read<T>: unsupported T");
      }
    }

    template <typename E>
      requires std::is_enum_v<E>
    auto ReadNative() -> E
    {
      return static_cast<E>(Read<std::int64_t>());
    }

    auto ReadBytes() -> std::vector<std::byte>
    {
      auto const wire{ Wire() };
      if (wire.TagAt(_cur) != TAG_STRING)
        ThrowType("octets");
      auto const payload{ wire.PayloadOf(_cur).bytes };
      return { payload.begin(), payload.end() };
    }

    auto IsNull() const -> bool { return Wire().TagAt(_cur) == TAG_NULL; }
    auto Subtree() const -> std::string
    {
      auto const wire{ Wire() };
      return std::string{ AsChars(wire.Slice(_cur, wire.NodeLength(_cur))) };
    }

    auto EnterObject() -> void
    {
      auto const wire{ Wire() };
      if (wire.TagAt(_cur) != TAG_OBJECT)
        ThrowType("object");
      auto const content{ wire.PayloadOf(_cur) };
      std::size_t const end{ content.at + content.bytes.size() };
      Level level{ .node = _cur };
      std::size_t pos{ content.at };
      if constexpr (!NAMED) {
        Cursor const bounded{ content.bytes };
        std::size_t offset{ 0 };
        auto const count{ bounded.Int(offset) };
        pos += offset;
        level.cursor = pos;
        level.end = end;
        for (U64 i{ 0 }; i < count; ++i)
          pos = wire.NextNode(pos, end);
        if (pos != end)
          throw ParseError{ "positional: object count leaves extra bytes" };
        _nav.push_back(std::move(level));
        return;
      }
      while (pos < end) {
        if (wire.TagAt(pos) != TAG_STRING)
          throw ParseError{ std::format(
              "binary: object key at offset {} is not a string", pos) };
        std::string name{ AsChars(wire.PayloadOf(pos).bytes) };
        auto const value{ wire.NextNode(pos, end) };
        level.fields.emplace(std::move(name), value);
        pos = wire.NextNode(value, end);
      }
      _nav.push_back(std::move(level));
    }
    auto LeaveObject() -> void
    {
      _cur = _nav.back().node;
      _nav.pop_back();
    }

    auto HasField(std::string_view name) const -> bool
    {
      if constexpr (!NAMED)
        return HasNext();
      return _nav.back().fields.contains(std::string{ name });
    }

    auto FieldNames() const -> std::vector<std::string>
    {
      std::vector<std::string> names;
      if constexpr (!NAMED)
        return names;
      for (auto const& [name, _] : _nav.back().fields)
        names.push_back(name);
      return names;
    }

    auto EnterField(std::string_view name) -> void
    {
      if constexpr (!NAMED) {
        if (!HasField(name))
          throw MissingField{ Path() / std::filesystem::path{ name } };
        EnterNext();
        _trail.back() = std::string{ name };
        return;
      }
      auto const found{ _nav.back().fields.find(std::string{ name }) };
      if (found == _nav.back().fields.end())
        throw MissingField{ Path() / std::filesystem::path{ name } };
      _cur = found->second;
      _trail.emplace_back(name);
    }
    auto LeaveField() -> void
    {
      if constexpr (!NAMED) {
        LeaveNext();
        return;
      }
      _cur = _nav.back().node;
      _trail.pop_back();
    }

    auto EnterArray() -> void
    {
      auto const wire{ Wire() };
      if (wire.TagAt(_cur) != TAG_ARRAY)
        ThrowType("array");
      auto const content{ wire.PayloadOf(_cur) };
      _nav.push_back(Level{ .node = _cur,
                            .cursor = content.at,
                            .end = content.at + content.bytes.size() });
    }
    auto LeaveArray() -> void
    {
      _cur = _nav.back().node;
      _nav.pop_back();
    }

    auto HasNext() const -> bool
    {
      return _nav.back().cursor < _nav.back().end;
    }
    // Arrays have no eager child walk, so containment must be checked on entry before any item read.
    auto EnterNext() -> void
    {
      auto& level{ _nav.back() };
      level.next = Wire().NextNode(level.cursor, level.end);
      _cur = level.cursor;
      _trail.emplace_back(ITEM);
    }
    auto LeaveNext() -> void
    {
      auto& level{ _nav.back() };
      level.cursor = level.next;
      _cur = level.node;
      _trail.pop_back();
    }

    // The trail is what a thrown error names, so it is spelled as a path
    // only when one is thrown.
    auto Path() const -> std::filesystem::path
    {
      std::filesystem::path path;
      for (auto const& name : _trail)
        path /= std::filesystem::path{ name };
      return path;
    }

  private:
    struct Level
    {
      std::size_t node{ 0 };
      std::unordered_map<std::string, std::size_t> fields;
      std::size_t cursor{ 0 };
      std::size_t next{ 0 };
      std::size_t end{ 0 };
    };

    static constexpr std::string_view ITEM{ "[]" };

    auto Wire() const -> Cursor { return Cursor{ Bytes{ _data } }; }
    [[noreturn]] auto ThrowType(std::string_view expected) const -> void
    {
      throw TypeMismatch{ Path(), std::string{ expected } };
    }

    Blob _data;
    std::size_t _cur{ 0 };
    std::vector<Level> _nav;
    std::vector<std::string> _trail;
  };
} // namespace oxbox::serialization::detail::binary_reader
