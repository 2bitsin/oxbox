#pragma once

#include "oxbox/serialization/binary-wire.hpp"
#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/stream.hpp"
#include "oxbox/utilities/serdes.hpp"

#include <bit>
#include <cstdint>
#include <type_traits>

namespace oxbox::serialization::detail::binary_writer
{
  template <Sink S, bool NAMED>
  class BinaryWriter : public binary_wire::BinaryWire
  {
  public:
    static constexpr bool PRESERVE_FIELD_SLOTS{ !NAMED };

    explicit BinaryWriter(S& sink) : _sink{ sink } {}

    auto Write(bool v) -> void
    {
      Scalar(TAG_BOOL, Blob{ std::byte{ static_cast<U08>(v ? 1 : 0) } });
    }
    auto Write(std::int64_t v) -> void { Integer(static_cast<U64>(v)); }
    auto Write(std::uint64_t v) -> void { Integer(v); }
    auto Write(double v) -> void
    {
      Scalar(TAG_DOUBLE, Fixed(std::bit_cast<U64>(v)));
    }
    auto Write(std::string_view v) -> void
    {
      Variable(TAG_STRING, oxbox::utilities::AsBytes(v));
    }
    auto WriteNull() -> void { Emit(Blob{ std::byte{ TAG_NULL } }); }

    template <typename E>
      requires std::is_enum_v<E>
    auto WriteNative(E value) -> void
    {
      Write(static_cast<std::int64_t>(std::to_underlying(value)));
    }

    auto BeginObject() -> void { _stack.push_back(Frame{ .tag = TAG_OBJECT }); }
    auto EndObject() -> void { Commit(); }
    auto BeginArray() -> void { _stack.push_back(Frame{ .tag = TAG_ARRAY }); }
    auto EndArray() -> void { Commit(); }

    auto Field(std::string_view name) -> void
    {
      if constexpr (NAMED)
        StringNode(_stack.back().content, name);
    }

    auto Flush() -> void { detail::PutBytes(_sink, Bytes{ _root }); }

  private:
    struct Frame
    {
      Tag tag;
      Blob content;
      U64 count{ 0 };
    };

    // The wire stores a double's bit pattern as eight little-endian bytes.
    static auto Fixed(U64 value) -> Blob
    {
      Blob out(sizeof(U64));
      oxbox::utilities::Store(value, oxbox::utilities::WritableBytes{ out },
                              oxbox::utilities::NoAdvance);
      return out;
    }

    auto Integer(U64 value) -> void
    {
      Blob node;
      WriteInt(node, value);
      Emit(node);
    }

    static auto StringNode(Blob& out, std::string_view text) -> void
    {
      out.push_back(std::byte{ TAG_STRING });
      WriteInt(out, text.size());
      Append(out, oxbox::utilities::AsBytes(text));
    }

    auto Emit(Bytes node) -> void
    {
      Append(_stack.empty() ? _root : _stack.back().content, node);
      if (!_stack.empty())
        ++_stack.back().count;
    }

    auto Scalar(Tag tag, Bytes content) -> void
    {
      Blob node{ std::byte{ static_cast<U08>(tag) } };
      Append(node, content);
      Emit(node);
    }

    auto Variable(Tag tag, Bytes content) -> void
    {
      Blob node{ std::byte{ static_cast<U08>(tag) } };
      WriteInt(node, content.size());
      Append(node, content);
      Emit(node);
    }

    auto Commit() -> void
    {
      Frame frame{ std::move(_stack.back()) };
      _stack.pop_back();
      if constexpr (!NAMED) {
        if (frame.tag == TAG_OBJECT) {
          Blob content;
          WriteInt(content, frame.count);
          Append(content, frame.content);
          Variable(frame.tag, content);
          return;
        }
      }
      Variable(frame.tag, frame.content);
    }

    S& _sink;
    Blob _root;
    std::vector<Frame> _stack;
  };

} // namespace oxbox::serialization::detail::binary_writer
