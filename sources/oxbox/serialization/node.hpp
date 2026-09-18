// NOLINTBEGIN(misc-non-private-member-variables-in-classes): the serialization DSL is a
// value/descriptor surface by design (module-wide ruling)
#pragma once

#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace oxbox::serialization::detail::node
{
  struct Node;

  using Object = std::map<std::string, Node>;
  using Array  = std::vector<Node>;

// 
  struct Node
  {
    std::variant<std::monostate, std::string, Array, Object> value;

    Node() = default;
    Node(std::string scalar) : value{ std::move(scalar) } {}
    Node(char const* scalar) : value{ std::string{ scalar } } {}
    Node(Array elements)     : value{ std::move(elements) } {}
    Node(Object members)     : value{ std::move(members) } {}

    [[nodiscard]] auto IsNull()   const -> bool { return std::holds_alternative<std::monostate>(value); }
    [[nodiscard]] auto IsScalar() const -> bool { return std::holds_alternative<std::string>(value); }
    [[nodiscard]] auto IsArray()  const -> bool { return std::holds_alternative<Array>(value); }
    [[nodiscard]] auto IsObject() const -> bool { return std::holds_alternative<Object>(value); }

    [[nodiscard]] auto Scalar()   const -> std::string const& { return std::get<std::string>(value); }
    [[nodiscard]] auto Elements() const -> Array const&       { return std::get<Array>(value); }
    [[nodiscard]] auto Members()  const -> Object const&      { return std::get<Object>(value); }
  };
}

namespace oxbox::serialization
{
  using detail::node::Node;
  using NodeObject = detail::node::Object;
  using NodeArray  = detail::node::Array;
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
