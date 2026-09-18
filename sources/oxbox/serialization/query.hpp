#pragma once
// Targeted introspection over a scheme without a whole-object dump:
// enumerate the fields, read one, modify one -- a single slot instead of
// serializing the whole thing.

#include "oxbox/serialization/io.hpp"          // StringSink / StringSource
#include "oxbox/serialization/read-walker.hpp"
#include "oxbox/serialization/write-walker.hpp"
#include "oxbox/utilities/visitor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

namespace oxbox::serialization
{
  enum class FieldForm { Boolean, Signed, Unsigned, Real, Text, Aggregate };

  class FieldInfo
  {
  public:
    constexpr FieldInfo(std::string_view name, FieldForm form) noexcept
    : _name{ name }
    , _form{ form }
    {}

    [[nodiscard]] constexpr auto Name() const -> std::string_view { return _name; }
    [[nodiscard]] constexpr auto Form() const -> FieldForm        { return _form; }

  private:
    std::string_view _name;
    FieldForm        _form;
  };
}

namespace oxbox::serialization::detail
{
  template <typename V>
  consteval auto FormOf() -> FieldForm
  {
    using Value = std::remove_cvref_t<V>;
    if      constexpr (std::same_as<Value, bool>)             return FieldForm::Boolean;
    else if constexpr (std::same_as<Value, std::string>
                    || std::same_as<Value, std::string_view>) return FieldForm::Text;
    else if constexpr (std::signed_integral<Value>)           return FieldForm::Signed;
    else if constexpr (std::unsigned_integral<Value>)         return FieldForm::Unsigned;
    else if constexpr (std::floating_point<Value>)            return FieldForm::Real;
    else                                                      return FieldForm::Aggregate;
  }

  // a compile-time count, so enumeration returns a fixed array and not a vector
  template <typename T>
  using SchemeTuple = std::remove_cvref_t<decltype(SchemeFor(std::declval<T const&>()).fields)>;

  template <typename T>
  inline constexpr std::size_t SchemeFieldCount = std::tuple_size_v<SchemeTuple<T>>;

  // fn receives the field object itself, whose Get/Set reach the value
  template <typename T, typename Fn>
    requires HasScheme<std::remove_cvref_t<T>>
  constexpr auto WithFieldNamed(T const& owner, std::string_view name, Fn&& fn) -> bool
  {
    bool matched{ false };
    std::apply([&](auto const&... fields) {
      auto one = [&](auto const& field) {
        if (matched || field.WireName() != name) return;
        fn(field);
        matched = true;
      };
      (one(fields), ...);
    }, SchemeFor(owner).fields);
    return matched;
  }

  // run leaf(finalObject, finalName) at the end of a dotted path; descent
  // stops at a field with no scheme, since a path cannot enter a scalar
  template <typename T, typename Leaf>
    requires HasScheme<std::remove_cvref_t<T>>
  constexpr auto DescendPath(T const& obj, std::string_view path, Leaf&& leaf) -> bool
  {
    auto const dot{ path.find('.') };
    if (dot == std::string_view::npos) return leaf(obj, path);
    bool done{ false };
    WithFieldNamed(obj, path.substr(0, dot), [&](auto const& field) {
      using Value = typename std::remove_cvref_t<decltype(field)>::value_type;
      if constexpr (HasScheme<Value>) done = DescendPath(field.Get(obj), path.substr(dot + 1), leaf);
    });
    return done;
  }

  // the mutable twin, descending through each field's mutable slot
  template <typename T, typename Leaf>
    requires HasScheme<std::remove_cvref_t<T>>
  constexpr auto DescendPathMut(T& obj, std::string_view path, Leaf&& leaf) -> bool
  {
    auto const dot{ path.find('.') };
    if (dot == std::string_view::npos) return leaf(obj, path);
    bool done{ false };
    WithFieldNamed(obj, path.substr(0, dot), [&](auto const& field) {
      using Value = typename std::remove_cvref_t<decltype(field)>::value_type;
      if constexpr (HasScheme<Value>)
        done = DescendPathMut(field.Ref(obj), path.substr(dot + 1), leaf);
    });
    return done;
  }

  // every segment is a sub-object here, unlike DescendPath, whose last
  // segment is a leaf name; an empty path visits obj itself
  template <typename T, typename Visit>
    requires HasScheme<std::remove_cvref_t<T>>
  constexpr auto DescendToObject(T const& obj, std::string_view path, Visit&& visit) -> bool
  {
    if (path.empty()) { visit(obj); return true; }
    auto const dot{ path.find('.') };
    auto const head{ path.substr(0, dot == std::string_view::npos ? path.size() : dot) };
    auto const tail{ dot == std::string_view::npos ? std::string_view{ } : path.substr(dot + 1) };
    bool done{ false };
    WithFieldNamed(obj, head, [&](auto const& field) {
      using Value = typename std::remove_cvref_t<decltype(field)>::value_type;
      if constexpr (HasScheme<Value>) done = DescendToObject(field.Get(obj), tail, visit);
    });
    return done;
  }
}

namespace oxbox::serialization
{
  template <typename T>
    requires HasScheme<T>
  constexpr auto FieldNames(T const& obj) -> std::array<std::string_view, detail::SchemeFieldCount<T>>
  {
    return std::apply([](auto const&... fields) {
      return std::array<std::string_view, sizeof...(fields)>{ fields.WireName()... };
    }, SchemeFor(obj).fields);
  }

  template <typename T>
    requires HasScheme<T>
  constexpr auto Fields(T const& obj) -> std::array<FieldInfo, detail::SchemeFieldCount<T>>
  {
    return std::apply([](auto const&... fields) {
      return std::array<FieldInfo, sizeof...(fields)>{ FieldInfo{ fields.WireName(),
        detail::FormOf<typename std::remove_cvref_t<decltype(fields)>::value_type>() }... };
    }, SchemeFor(obj).fields);
  }

  // the child names at a path, empty when it is a scalar leaf or does not
  // resolve; runtime-sized, hence a vector
  template <typename T>
    requires HasScheme<T>
  auto FieldNamesAt(T const& obj, std::string_view path) -> std::vector<std::string>
  {
    std::vector<std::string> keys;
    detail::DescendToObject(obj, path, [&keys](auto const& node) {
      for (auto const name : FieldNames(node)) keys.emplace_back(name);
    });
    return keys;
  }

  template <typename T>
    requires HasScheme<T>
  auto HasFieldNamed(T const& obj, std::string_view path) -> bool
  {
    return detail::DescendPath(obj, path, [](auto const& node, std::string_view name) {
      return detail::WithFieldNamed(node, name, [](auto const&) {});
    });
  }

  // enumerate and read in one scheme walk, not a name-by-name re-walk
  template <typename T, typename Visitor>
    requires HasScheme<T>
  constexpr auto ForEachField(T const& obj, Visitor&& visit) -> void
  {
    std::apply([&](auto const&... fields) {
      (visit(FieldInfo{ fields.WireName(),
         detail::FormOf<typename std::remove_cvref_t<decltype(fields)>::value_type>() },
         fields.Get(obj)), ...);
    }, SchemeFor(obj).fields);
  }

  template <typename F, Sink S, typename T>
    requires HasScheme<T>
  auto SerializeField(T const& obj, std::string_view path, S& sink) -> bool
  {
    return detail::DescendPath(obj, path, [&sink](auto const& node, std::string_view name) {
      return detail::WithFieldNamed(node, name, [&sink, &node](auto const& field) {
        typename F::template Writer<S> writer{ sink };
        detail::WriteWalker walker{ writer };
        walker(field.Get(node));
        writer.Flush();
      });
    });
  }

  template <typename F, typename T>
    requires HasScheme<T>
  auto SerializeField(T const& obj, std::string_view name) -> std::optional<std::string>
  {
    StringSink sink;
    if (!SerializeField<F>(obj, name, sink)) return std::nullopt;
    return std::move(sink).Out();
  }

  // the value as V when it converts, and nothing otherwise
  template <typename V, typename T>
    requires HasScheme<T>
  auto GetField(T const& obj, std::string_view path) -> std::optional<V>
  {
    std::optional<V> result;
    detail::DescendPath(obj, path, [&result](auto const& node, std::string_view name) {
      return detail::WithFieldNamed(node, name, [&result, &node](auto const& field) {
        using Value = typename std::remove_cvref_t<decltype(field)>::value_type;
        if constexpr (std::convertible_to<Value, V>) result = static_cast<V>(field.Get(node));
      });
    });
    return result;
  }

  template <typename F, Source Src, typename T>
    requires HasScheme<T>
  auto DeserializeField(T& obj, std::string_view path, Src& source) -> bool
  {
    return detail::DescendPathMut(obj, path, [&source](auto& node, std::string_view name) {
      return detail::WithFieldNamed(node, name, [&source, &node](auto const& field) {
        using Value = typename std::remove_cvref_t<decltype(field)>::value_type;
        typename F::template Reader<Src> reader{ source };
        detail::ReadWalker walker{ reader };
        Value tmp{ };
        walker(tmp);
        field.Set(node, std::move(tmp));
      });
    });
  }

  template <typename F, typename T>
    requires HasScheme<T>
  auto DeserializeField(T& obj, std::string_view name, std::string_view wire) -> bool
  {
    StringSource source{ wire };
    return DeserializeField<F>(obj, name, source);
  }

  // assigned only when V converts to the field's type
  template <typename V, typename T>
    requires HasScheme<T>
  auto SetField(T& obj, std::string_view path, V const& value) -> bool
  {
    bool assigned{ false };
    detail::DescendPathMut(obj, path, [&assigned, &value](auto& node, std::string_view name) {
      return detail::WithFieldNamed(node, name, [&assigned, &value, &node](auto const& field) {
        using Value = typename std::remove_cvref_t<decltype(field)>::value_type;
        if constexpr (std::convertible_to<V, Value>) { field.Set(node, static_cast<Value>(value)); assigned = true; }
      });
    });
    return assigned;
  }

  // a leaf value as a tagged scalar, which pybind converts straight to a
  // py::object; monostate is an unresolved path or a non-scalar field
  using FieldScalar =
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string>;

  template <typename T>
    requires HasScheme<T>
  auto GetScalar(T const& obj, std::string_view path) -> FieldScalar
  {
    FieldScalar result;
    detail::DescendPath(obj, path, [&result](auto const& node, std::string_view name) {
      return detail::WithFieldNamed(node, name, [&result, &node](auto const& field) {
        using Value = typename std::remove_cvref_t<decltype(field)>::value_type;
        if      constexpr (std::same_as<Value, bool>)        result = field.Get(node);
        else if constexpr (std::signed_integral<Value>)      result = static_cast<std::int64_t>(field.Get(node));
        else if constexpr (std::unsigned_integral<Value>)    result = static_cast<std::uint64_t>(field.Get(node));
        else if constexpr (std::floating_point<Value>)       result = static_cast<double>(field.Get(node));
        else if constexpr (std::convertible_to<Value, std::string_view>)
          result = std::string{ std::string_view{ field.Get(node) } };
      });
    });
    return result;
  }

  // false when the path does not resolve or the scalar does not fit
  template <typename T>
    requires HasScheme<T>
  auto SetScalar(T& obj, std::string_view path, FieldScalar const& value) -> bool
  {
    return std::visit(oxbox::utilities::Visitor{
      [](std::monostate) { return false; },
      [&](auto const& held) { return SetField<std::remove_cvref_t<decltype(held)>>(obj, path, held); },
    }, value);
  }
}
