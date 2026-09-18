// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests
#include "oxbox/serialization/format-node.hpp"

#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
  // lowercase on the wire, uppercase in the source: not the enumerators'
  // own spellings, so each carries a LABEL -- `_Label(red) RED` at the
  // declaration in a generated header, written out here because a
  // test-local enum never reaches the generator
  enum class Color : std::uint8_t { RED, GREEN, BLUE };

  constexpr auto reflect_scheme(Color*)
  {
    using E = Color;
    return ::reflect::enum_scheme<
      ::reflect::enumerator<"RED",   E::RED,   "", "red">,
      ::reflect::enumerator<"GREEN", E::GREEN, "", "green">,
      ::reflect::enumerator<"BLUE",  E::BLUE,  "", "blue">>{ };
  }

  struct Rec
  {
    friend constexpr auto reflect_scheme(Rec*);

    std::string                name;
    std::int64_t               count;
    Color                      color;
    std::optional<std::string> note;
    std::vector<std::string>   tags;

    auto operator==(Rec const&) const -> bool = default;
  };

  constexpr auto reflect_scheme(Rec*)
  {
    using T = Rec;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"name",  &T::name>,
      ::reflect::member_scheme<"count", &T::count>,
      ::reflect::member_scheme<"color", &T::color>,
      ::reflect::member_scheme<"note",  &T::note>,
      ::reflect::member_scheme<"tags",  &T::tags>>{ };
  }

  using oxbox::serialization::FromNode;
  using oxbox::serialization::Node;
  using oxbox::serialization::NodeArray;
  using oxbox::serialization::NodeObject;
}


TEST(NodeFormat, DeserializesTypedRecordFromAnStlNode)
{
  Node const tree{ NodeObject{
    { "name",  Node{ "vaddpd" } },
    { "count", Node{ "3" } },
    { "color", Node{ "green" } },
    { "note",  Node{ "hi" } },
    { "tags",  Node{ NodeArray{ Node{ "a" }, Node{ "b" } } } },
  } };

  EXPECT_EQ(FromNode<Rec>(tree),
            (Rec{ "vaddpd", 3, Color::GREEN, "hi", { "a", "b" } }));
}

TEST(NodeFormat, OmittedOptionalStaysNullopt)
{
  Node const tree{ NodeObject{
    { "name",  Node{ "nop" } },
    { "count", Node{ "0" } },
    { "color", Node{ "red" } },
    { "tags",  Node{ NodeArray{} } },
  } };

  auto const rec{ FromNode<Rec>(tree) };
  EXPECT_FALSE(rec.note.has_value());
  EXPECT_TRUE(rec.tags.empty());
}

TEST(NodeFormat, ANonNumericScalarForANumberFieldThrows)
{
  Node const tree{ NodeObject{
    { "name",  Node{ "x" } },
    { "count", Node{ "not-a-number" } },
    { "color", Node{ "red" } },
    { "tags",  Node{ NodeArray{} } },
  } };

  EXPECT_THROW((void)FromNode<Rec>(tree), oxbox::serialization::TypeMismatch);
}

TEST(NodeFormat, AnUnknownEnumStringThrows)
{
  Node const tree{ NodeObject{
    { "name",  Node{ "x" } },
    { "count", Node{ "1" } },
    { "color", Node{ "chartreuse" } },
    { "tags",  Node{ NodeArray{} } },
  } };

  EXPECT_THROW((void)FromNode<Rec>(tree), oxbox::serialization::ParseError);
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
