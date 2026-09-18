#pragma once

#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/node.hpp"
#include "oxbox/serialization/read-walker.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oxbox::serialization
{
// 
  struct NodeFormat
  {
    template <Source Src> class Reader;
  };

  template <Source Src>
  class NodeFormat::Reader
  {
  public:
    explicit Reader(Node const& root) { _stack.push_back(Frame{ &root }); }

    template <typename T>
    auto Read() -> T
    {
      auto const& node{ Current() };
      if (!node.IsScalar()) ThrowType("scalar");
      auto const& text{ node.Scalar() };

      if constexpr (std::same_as<T, std::string>) {
        return text;
      } else if constexpr (std::same_as<T, bool>) {
        if (text == "true"  || text == "1") return true;
        if (text == "false" || text == "0") return false;
        ThrowType("bool");
      } else {
        T out{};
        auto const* const first{ text.data() };
        auto const* const last { text.data() + text.size() };
        auto const [stop, error]{ std::from_chars(first, last, out) };
        if (error != std::errc{} || stop != last) ThrowType("number");
        return out;
      }
    }

    [[nodiscard]] auto IsNull() const -> bool { return Current().IsNull(); }
    [[nodiscard]] auto Subtree() const -> Node { return Current(); }

    auto EnterObject() -> void { if (!Current().IsObject()) ThrowType("object"); }
    auto LeaveObject() -> void {}

    [[nodiscard]] auto HasField(std::string_view name) const -> bool
    {
      auto const& node{ Current() };
      return node.IsObject() && node.Members().contains(std::string{ name });
    }

    [[nodiscard]] auto FieldNames() const -> std::vector<std::string>
    {
      std::vector<std::string> names;
      for (auto const& [key, value] : Current().Members()) names.push_back(key);
      return names;
    }

    auto EnterField(std::string_view name) -> void
    {
      auto const& members{ Current().Members() };
      auto const slot{ members.find(std::string{ name }) };
      if (slot == members.end())
        throw MissingField{ _path / std::filesystem::path{ name } };
      _stack.push_back(Frame{ &slot->second });
      _path /= std::filesystem::path{ name };
    }
    auto LeaveField() -> void { _stack.pop_back(); _path = _path.parent_path(); }

    auto EnterArray() -> void { if (!Current().IsArray()) ThrowType("array"); }
    auto LeaveArray() -> void {}

    [[nodiscard]] auto HasNext() const -> bool
    {
      return _stack.back().index < Current().Elements().size();
    }
    auto EnterNext() -> void
    {
      auto const index{ _stack.back().index };
      _stack.push_back(Frame{ &Current().Elements()[index] });
      _path /= std::filesystem::path{ std::to_string(index) };
    }
    auto LeaveNext() -> void
    {
      _stack.pop_back();
      ++_stack.back().index;
      _path = _path.parent_path();
    }

    [[nodiscard]] auto Path() const -> std::filesystem::path const& { return _path; }

  private:
    struct Frame
    {
      Node const* node;
      std::size_t index{ 0 };
    };

    [[nodiscard]] auto Current() const -> Node const& { return *_stack.back().node; }

    [[noreturn]] auto ThrowType(std::string_view expected) const -> void
    {
      throw TypeMismatch{ _path, std::string{ expected } };
    }

    std::vector<Frame>    _stack;
    std::filesystem::path _path;
  };

// 
  template <typename T>
  auto FromNode(Node const& node) -> T
  {
    typename NodeFormat::template Reader<detail::ProbeSource> reader{ node };
    detail::ReadWalker walker{ reader };
    T out{};
    walker(out);
    if constexpr (HasRestoreHook<T>) RunRestoreHook(out);
    return out;
  }
}
