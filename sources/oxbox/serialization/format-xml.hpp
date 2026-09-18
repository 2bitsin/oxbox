// NOLINTBEGIN(misc-non-private-member-variables-in-classes): the serialization DSL is a
// value/descriptor surface by design (module-wide ruling)
#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pugixml.hpp>

#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/stream.hpp"
#include "oxbox/serialization/serializable.hpp"
#include "oxbox/utilities/path.hpp"

namespace oxbox::serialization
{
  namespace detail::xml
  {
    inline constexpr std::string_view ROOT_TAG { "root" };
    inline constexpr std::string_view ITEM_TAG { "item" };
    inline constexpr std::string_view NIL_ATTR { "nil"  };

    // A pugi::xml_writer that appends into a std::string. Kept at namespace
    // scope, NOT local to WriterCore::Flush: a local class inside a template
    // member function that overrides a virtual trips a VS 2026 parser bug
    // (C2187 "'override' unexpected"). It carries no template dependency, so
    // hoisting it is a clean win as well as the portable workaround.
    struct StringXmlWriter : pugi::xml_writer
    {
      std::string& out;
      explicit StringXmlWriter(std::string& o) : out{ o } {}
      auto write(void const* data, std::size_t size) -> void override
      {
        out.append(static_cast<char const*>(data), size);
      }
    };

    template <Sink S>
    class WriterCore
    {
    public:
      WriterCore(S& sink, unsigned save_flags)
      : _sink{ sink }, _save_flags{ save_flags } {}

      auto Write(bool v) -> void {
        _PutText(v ? "true" : "false");
      }
      auto Write(std::int64_t v) -> void {
        char buf[24];
        auto const [end, ec]{ std::to_chars(buf, buf + sizeof(buf), v) };
        _PutText(std::string_view{ buf, end });
      }
      auto Write(std::uint64_t v) -> void {
        char buf[24];
        auto const [end, ec]{ std::to_chars(buf, buf + sizeof(buf), v) };
        _PutText(std::string_view{ buf, end });
      }
      auto Write(double v) -> void {
        char buf[32];
        auto const [end, ec]{ std::to_chars(buf, buf + sizeof(buf), v) };
        _PutText(std::string_view{ buf, end });
      }
      auto Write(std::string_view v) -> void {
        _PutText(v);
      }
      auto WriteNull() -> void {
        auto node{ _Allocate() };
        node.append_attribute(NIL_ATTR.data()) = "true";
      }

      auto BeginObject() -> void { _BeginContainer(/*is_array*/ false); }
      auto EndObject  () -> void { _stack.pop_back(); }
      auto BeginArray () -> void { _BeginContainer(/*is_array*/ true);  }
      auto EndArray   () -> void { _stack.pop_back(); }

      auto Field(std::string_view name) -> void {
        _stack.back().pending_field = std::string{ name };
      }

      auto Flush() -> void
      {
        std::string out;
        StringXmlWriter w{ out };
        _doc.save(w, "  ", _save_flags);
        detail::PutText(_sink, out);
      }

    private:
      auto _Allocate() -> pugi::xml_node
      {
        if (_stack.empty()) {
          auto node{ _doc.append_child(ROOT_TAG.data()) };
          _stack.push_back({ node, false, {} });
          return node;
        }
        auto& frame{ _stack.back() };
        auto const tag{
          frame.is_array
            ? std::string{ ITEM_TAG }
            : std::move(*frame.pending_field) };
        frame.pending_field.reset();
        return frame.node.append_child(tag.c_str());
      }

      auto _PutText(std::string_view text) -> void
      {
        auto node{ _Allocate() };
        if (!text.empty())
          node.append_child(pugi::node_pcdata).set_value(
            std::string{ text }.c_str());
      }

      auto _BeginContainer(bool is_array) -> void
      {
        auto node{ _Allocate() };
        _stack.push_back({ node, is_array, {} });
      }

      struct Frame {
        pugi::xml_node             node;
        bool                       is_array;
        std::optional<std::string> pending_field;
      };

      S&                  _sink;
      unsigned            _save_flags;
      pugi::xml_document  _doc;
      std::vector<Frame>  _stack;
    };

    template <Source Src>
    class ReaderCore
    {
    public:
      explicit ReaderCore(Src& source)
      {
        // the DOM builds straight off the stream -- raw text is never held
        detail::SourceStream stream{ source };
        auto const result{ _doc.load(stream.Stream()) };
        if (!result)
          throw oxbox::serialization::ParseError{
            std::string{ "xml parse error: " } + result.description() };
        _cur = _doc.child(ROOT_TAG.data());
        if (!_cur)
          throw oxbox::serialization::ParseError{ "xml: missing <root>" };
      }

      template <typename T>
      auto Read() -> T
      {
        auto const text{ _TextOf(_cur) };
        if constexpr (std::same_as<T, bool>) {
          if      (text == "true" ) return true;
          else if (text == "false") return false;
          _ThrowType("bool");
        } else if constexpr (std::same_as<T, std::string>) {
          return std::string{ text };
        } else if constexpr (std::same_as<T, std::int64_t>) {
          std::int64_t v{};
          auto const [p, ec]{
            std::from_chars(text.data(), text.data() + text.size(), v) };
          if (ec != std::errc{} || p != text.data() + text.size())
            _ThrowType("integer");
          return v;
        } else if constexpr (std::same_as<T, std::uint64_t>) {
          std::uint64_t v{};
          auto const [p, ec]{
            std::from_chars(text.data(), text.data() + text.size(), v) };
          if (ec != std::errc{} || p != text.data() + text.size())
            _ThrowType("integer");
          return v;
        } else if constexpr (std::same_as<T, double>) {
          double v{};
          auto const [p, ec]{
            std::from_chars(text.data(), text.data() + text.size(), v) };
          if (ec != std::errc{} || p != text.data() + text.size())
            _ThrowType("number");
          return v;
        } else {
          static_assert(sizeof(T) == 0,
            "XmlFormat::Reader::Read<T>: unsupported T");
        }
      }

      auto IsNull() const -> bool {
        auto const nil{ _cur.attribute(NIL_ATTR.data()) };
        return nil && std::string_view{ nil.value() } == "true";
      }

      auto EnterObject() -> void {
        _nav.push_back(Level{ .parent = _cur });
      }
      auto LeaveObject() -> void { _nav.pop_back(); }

      auto HasField(std::string_view name) const -> bool {
        return static_cast<bool>(
          _nav.back().parent.child(std::string{ name }.c_str()));
      }

      auto FieldNames() const -> std::vector<std::string> {
        std::vector<std::string> out;
        for (auto child : _nav.back().parent.children())
          out.emplace_back(child.name());
        return out;
      }

      auto EnterField(std::string_view name) -> void {
        using namespace utilities;
        auto const key{ std::string{ name } };
        auto child{ _nav.back().parent.child(key.c_str()) };
        if (!child)
          throw oxbox::serialization::MissingField{
            _path / PathFromString(name) };
        _cur = child;
        _path /= PathFromString(name);
      }

      auto LeaveField() -> void {
        _cur = _nav.back().parent;
        _path = _path.parent_path();
      }

      auto EnterArray() -> void {
        _nav.push_back(Level{
          .parent = _cur,
          .next   = _cur.first_child(),
        });
      }
      auto LeaveArray() -> void { _nav.pop_back(); }

      auto HasNext() const -> bool {
        return static_cast<bool>(_nav.back().next);
      }
      auto EnterNext() -> void {
        auto& level{ _nav.back() };
        _cur = level.next;
        _path /= PathFromString(
          std::to_string(level.idx));
      }
      auto LeaveNext() -> void {
        auto& level{ _nav.back() };
        _cur = level.parent;
        _path = _path.parent_path();
        level.next = level.next.next_sibling();
        ++level.idx;
      }

      auto Path() const -> std::filesystem::path const& { return _path; }

      auto Subtree() const -> pugi::xml_node { return _cur; }

    private:
      [[noreturn]] auto _ThrowType(std::string_view expected) const -> void {
        throw oxbox::serialization::TypeMismatch{ _path, std::string{ expected } };
      }
      static auto _TextOf(pugi::xml_node node) -> std::string_view {
        return node.child_value();
      }

      struct Level {
        pugi::xml_node parent;
        pugi::xml_node next;   // for arrays: the next <item> to read
        std::size_t    idx{ 0 };
      };

      pugi::xml_document  _doc;
      pugi::xml_node      _cur;
      std::vector<Level>  _nav;
      std::filesystem::path _path;
    };

    template <Sink S, unsigned FLAGS>
    class WriterFor : public WriterCore<S>
    {
    public:
      explicit WriterFor(S& sink) : WriterCore<S>(sink, FLAGS) {}
    };
  }

  struct XmlFormat
  {
    template <Sink S>     using Writer = detail::xml::WriterFor<S,
      pugi::format_raw | pugi::format_no_declaration>;
    template <Source Src> using Reader = detail::xml::ReaderCore<Src>;
  };

  struct XmlPrettyFormat
  {
    template <Sink S>     using Writer = detail::xml::WriterFor<S,
      pugi::format_indent | pugi::format_no_declaration>;
    template <Source Src> using Reader = detail::xml::ReaderCore<Src>;
  };
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
