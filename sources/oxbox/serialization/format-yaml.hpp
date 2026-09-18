#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/stream.hpp"
#include "oxbox/serialization/serializable.hpp"
#include "oxbox/utilities/path.hpp"

namespace oxbox::serialization
{
  struct YamlFormat {
    template <Sink S>     class Writer;
    template <Source Src> class Reader;
  };

  // ── YamlFormat::Writer<S> ──────────────────────────────────────────
  template <Sink S>
  class YamlFormat::Writer
  {
  public:
    explicit Writer(S& sink) : _sink{sink} {}

    auto Write(bool v)             -> void { _em << v; }
    auto Write(std::int64_t v)     -> void { _em << v; }
    auto Write(std::uint64_t v)    -> void { _em << v; }
    auto Write(double v)           -> void { _em << v; }
    auto Write(std::string_view v) -> void { _em << std::string{v}; }
    auto WriteNull()               -> void { _em << YAML::Null; }

    auto BeginObject() -> void { _em << YAML::BeginMap; }

    auto Field(std::string_view name) -> void {
      _em << YAML::Key << std::string{name} << YAML::Value;
    }

    auto EndObject() -> void { _em << YAML::EndMap; }

    auto BeginArray() -> void { _em << YAML::BeginSeq; }
    auto EndArray()   -> void { _em << YAML::EndSeq; }

    auto Flush() -> void {
      detail::PutText(_sink, std::string_view{ _em.c_str(), _em.size() });
      detail::PutText(_sink, "\n");
    }

  private:
    S& _sink;
    YAML::Emitter _em;
  };


  // ── YamlFormat::Reader<Src> ────────────────────────────────────────
  template <Source Src>
  class YamlFormat::Reader
  {
  public:
    // the DOM builds straight off the stream -- raw text is never held
    explicit Reader(Src& source) {
      detail::SourceStream stream{ source };
      try {
        _root = YAML::Load(stream.Stream());
      } catch (YAML::ParserException const& e) {
        throw oxbox::serialization::ParseError{e.what()};
      }
    }

// 
    explicit Reader(YAML::Node node) {
      _root.reset(node);
    }

    template <typename T>
    auto Read() -> T {
      auto cur = _Resolve();
      try {
        if constexpr (std::same_as<T, bool>) {
          if (!cur.IsScalar()) _ThrowType("bool");
          return cur.template as<bool>();
        } else if constexpr (std::same_as<T, std::string>) {
          if (!cur.IsScalar()) _ThrowType("string");
          return cur.template as<std::string>();
        } else if constexpr (std::same_as<T, std::int64_t>) {
          if (!cur.IsScalar()) _ThrowType("integer");
          return cur.template as<std::int64_t>();
        } else if constexpr (std::same_as<T, std::uint64_t>) {
          if (!cur.IsScalar()) _ThrowType("integer");
          return cur.template as<std::uint64_t>();
        } else if constexpr (std::same_as<T, double>) {
          if (!cur.IsScalar()) _ThrowType("number");
          return cur.template as<double>();
        } else {
          static_assert(sizeof(T) == 0, "YamlFormat::Reader::Read<T>: unsupported T");
        }
      } catch (YAML::Exception const&) {
        _ThrowType([] {
          if constexpr (std::same_as<T, bool>)               return "bool";
          else if constexpr (std::same_as<T, std::string>)   return "string";
          else if constexpr (std::same_as<T, double>)        return "number";
          else                                                return "integer";
        }());
      }
    }

    auto IsNull() const -> bool {
      auto cur = _Resolve();
      return !cur || cur.IsNull();
    }

    auto EnterObject() -> void {
      if (!_Resolve().IsMap()) _ThrowType("map");
      _stack.push_back(Frame{ .is_array = false });
    }

    auto LeaveObject() -> void { _stack.pop_back(); }

    auto HasField(std::string_view name) const -> bool {
      auto parent = _Resolve();
      if (!parent.IsMap()) return false;
      return parent[std::string{name}].IsDefined();
    }

    auto FieldNames() const -> std::vector<std::string> {
      auto parent = _Resolve();
      std::vector<std::string> out;
// 
      for (auto const& kv : parent) {
        out.push_back(kv.first.template as<std::string>());
      }
      return out;
    }

    auto EnterField(std::string_view name) -> void {
      if (!HasField(name)) {
        throw oxbox::serialization::MissingField{
          _path / PathFromString(name)};
      }
      _stack.back().pending = std::string{name};
      _path /= PathFromString(name);
    }

    auto LeaveField() -> void {
      _stack.back().pending.reset();
      _path = _path.parent_path();
    }

    auto EnterArray() -> void {
      if (!_Resolve().IsSequence()) _ThrowType("sequence");
      _stack.push_back(Frame{ .is_array = true });
    }

    auto LeaveArray() -> void { _stack.pop_back(); }

    auto HasNext() const -> bool {
// 
      auto arr = _ResolveTo(_stack.size() - 1);
      return _stack.back().idx < arr.size();
    }

    auto EnterNext() -> void {
      _stack.back().pending_idx = _stack.back().idx;
      _path /= PathFromString(std::to_string(_stack.back().idx));
    }

    auto LeaveNext() -> void {
      _stack.back().pending_idx.reset();
      _path = _path.parent_path();
      ++_stack.back().idx;
    }

    auto Path() const -> std::filesystem::path const& { return _path; }

    auto Subtree() const -> YAML::Node { return _Resolve(); }

  private:
    auto _Resolve() const -> YAML::Node {
      return _ResolveTo(_stack.size());
    }

// 
    auto _ResolveTo(std::size_t depth) const -> YAML::Node {
      YAML::Node cur;          // start undefined
      cur.reset(_root);
      for (std::size_t i = 0; i < depth; ++i) {
        auto const& f = _stack[i];
        if (f.is_array) {
          if (f.pending_idx) cur.reset(cur[*f.pending_idx]);
        } else {
          if (f.pending) cur.reset(cur[*f.pending]);
        }
      }
      return cur;
    }

    [[noreturn]] auto _ThrowType(std::string_view expected) const -> void {
      throw oxbox::serialization::TypeMismatch{_path, std::string{expected}};
    }

    struct Frame {
      bool is_array;
// 
      std::optional<std::string> pending;
// 
      std::size_t idx = 0;
      std::optional<std::size_t> pending_idx;
    };

    YAML::Node _root;
    std::vector<Frame> _stack;
    std::filesystem::path _path;
  };
}
