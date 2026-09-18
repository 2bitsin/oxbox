#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/stream.hpp"
#include "oxbox/serialization/serializable.hpp"
#include "oxbox/utilities/path.hpp"

namespace oxbox::serialization
{
  using utilities::PathFromString;
  using utilities::PathToString;
  struct JsonFormat {
    template <Sink S>     class Writer;
    template <Source Src> class Reader;
  };

  // ── JsonFormat::Writer<S> ──────────────────────────────────────────
  template <Sink S>
  class JsonFormat::Writer
  {
  public:
    explicit Writer(S& sink) : _sink{sink} {}

    auto Write(bool v)             -> void { _Put(v); }
    auto Write(std::int64_t v)     -> void { _Put(v); }
    auto Write(std::uint64_t v)    -> void { _Put(v); }
    auto Write(double v)           -> void { _Put(v); }
    auto Write(std::string_view v) -> void { _Put(std::string{v}); }
    auto WriteNull()               -> void { _Put(nullptr); }

    auto BeginObject() -> void {
      _stack.push_back(Frame{
        .node = nlohmann::json::object(),
        .is_object = true,
      });
    }

    auto Field(std::string_view name) -> void {
      _stack.back().pending_field = std::string{name};
    }

    auto EndObject() -> void { _Commit(); }

    auto BeginArray() -> void {
      _stack.push_back(Frame{
        .node = nlohmann::json::array(),
        .is_object = false,
      });
    }

    auto EndArray() -> void { _Commit(); }

    auto Flush() -> void {
      detail::PutText(_sink, _root.dump());
    }

  private:
    struct Frame {
      nlohmann::json node;
      bool is_object;
      std::optional<std::string> pending_field;
    };

// 
    template <typename V>
    auto _Put(V&& v) -> void {
      if (_stack.empty()) { _root = std::forward<V>(v); return; }
      auto& f = _stack.back();
      if (f.is_object) f.node[*f.pending_field] = std::forward<V>(v);
      else             f.node.push_back(std::forward<V>(v));
    }

    // Commit the top frame's accumulated node into its parent (or root).
    auto _Commit() -> void {
      auto frame = std::move(_stack.back());
      _stack.pop_back();
      _Put(std::move(frame.node));
    }

    S& _sink;
    nlohmann::json _root;
    std::vector<Frame> _stack;
  };


  // ── JsonFormat::Reader<Src> ────────────────────────────────────────
  template <Source Src>
  class JsonFormat::Reader
  {
  public:
    // the DOM builds straight off the stream -- raw text is never held
    explicit Reader(Src& source) {
      detail::SourceStream stream{ source };
      try {
        _root = nlohmann::json::parse(stream.Stream());
      } catch (nlohmann::json::parse_error const& e) {
        throw oxbox::serialization::ParseError{e.what()};
      }
      _cur = &_root;
    }

// 
    explicit Reader(nlohmann::json node) : _root(std::move(node)), _cur{&_root} {}

    template <typename T>
    auto Read() -> T {
      if constexpr (std::same_as<T, bool>) {
        if (!_cur->is_boolean()) _ThrowType("bool");
        return _cur->get<bool>();
      } else if constexpr (std::same_as<T, std::string>) {
        if (!_cur->is_string()) _ThrowType("string");
        return _cur->get<std::string>();
      } else if constexpr (std::same_as<T, std::int64_t>) {
        if (!_cur->is_number_integer() && !_cur->is_number_unsigned())
          _ThrowType("integer");
        return _cur->get<std::int64_t>();
      } else if constexpr (std::same_as<T, std::uint64_t>) {
        if (!_cur->is_number_integer() && !_cur->is_number_unsigned())
          _ThrowType("integer");
        return _cur->get<std::uint64_t>();
      } else if constexpr (std::same_as<T, double>) {
        if (!_cur->is_number()) _ThrowType("number");
        return _cur->get<double>();
      } else {
        static_assert(sizeof(T) == 0, "JsonFormat::Reader::Read<T>: unsupported T");
      }
    }

    auto IsNull() const -> bool { return _cur->is_null(); }

    auto EnterObject() -> void {
      if (!_cur->is_object()) _ThrowType("object");
      Level lvl{ .parent = _cur };
      _nav.push_back(std::move(lvl));
    }

    auto LeaveObject() -> void { _nav.pop_back(); }

    auto HasField(std::string_view name) const -> bool {
      return _nav.back().parent->contains(std::string{name});
    }

    auto FieldNames() const -> std::vector<std::string> {
      std::vector<std::string> out;
      out.reserve(_nav.back().parent->size());
      for (auto const& [k, _] : _nav.back().parent->items()) out.push_back(k);
      return out;
    }

    auto EnterField(std::string_view name) -> void {
      auto const key = std::string{name};
      if (!_nav.back().parent->contains(key)) {
        throw oxbox::serialization::MissingField{
          _path / PathFromString(name)};
      }
      _cur = &(*_nav.back().parent)[key];
      _path /= PathFromString(name);
    }

    auto LeaveField() -> void {
      _cur = _nav.back().parent;
      _path = _path.parent_path();
    }

    auto EnterArray() -> void {
      if (!_cur->is_array()) _ThrowType("array");
      Level lvl{ .parent = _cur };
      _nav.push_back(std::move(lvl));
    }

    auto LeaveArray() -> void { _nav.pop_back(); }

    auto HasNext() const -> bool {
      auto const& l = _nav.back();
      return l.array_idx < l.parent->size();
    }

    auto EnterNext() -> void {
      auto& l = _nav.back();
      _cur = &(*l.parent)[l.array_idx];
      _path /= PathFromString(std::to_string(l.array_idx));
    }

    auto LeaveNext() -> void {
      auto& l = _nav.back();
      _cur = l.parent;
      _path = _path.parent_path();
      ++l.array_idx;
    }

    auto Path() const -> std::filesystem::path const& { return _path; }

// 
    auto Subtree() const -> nlohmann::json { return *_cur; }

  private:
    [[noreturn]] auto _ThrowType(std::string_view expected) const -> void {
      throw oxbox::serialization::TypeMismatch{_path, std::string{expected}};
    }

    struct Level {
      nlohmann::json const* parent;
      std::size_t array_idx = 0;
    };

    nlohmann::json _root;
    nlohmann::json const* _cur;
    std::vector<Level> _nav;
    std::filesystem::path _path;
  };
}
