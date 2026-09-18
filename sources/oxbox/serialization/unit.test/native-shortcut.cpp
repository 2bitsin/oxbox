// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests
#include "oxbox/serialization/serializable.hpp"

#include "oxbox/serialization/concepts.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
  struct SpecialType
  {
    friend constexpr auto reflect_scheme(SpecialType*);

    std::int64_t a;
    std::int64_t b;

    auto operator==(SpecialType const&) const -> bool = default;
  };

  struct Wrapper
  {
    friend constexpr auto reflect_scheme(Wrapper*);

    SpecialType  special;
    std::int64_t plain;
  };

  constexpr auto reflect_scheme(SpecialType*)
  {
    using T = SpecialType;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"a", &T::a>,
      ::reflect::member_scheme<"b", &T::b>>{ };
  }

  constexpr auto reflect_scheme(Wrapper*)
  {
    using T = Wrapper;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"special", &T::special>,
      ::reflect::member_scheme<"plain",   &T::plain>>{ };
  }

  // A WriterBackend that records the primitive operations it is asked to do.
  struct TraceWriter
  {
    std::string trace;

    auto Write(bool) -> void             { trace += 'b'; }
    auto Write(std::int64_t) -> void     { trace += 'i'; }
    auto Write(std::uint64_t) -> void    { trace += 'u'; }
    auto Write(double) -> void           { trace += 'd'; }
    auto Write(std::string_view) -> void { trace += 's'; }
    auto WriteNull() -> void             { trace += 'z'; }
    auto BeginObject() -> void           { trace += '{'; }
    auto Field(std::string_view) -> void { trace += 'f'; }
    auto EndObject() -> void             { trace += '}'; }
    auto BeginArray() -> void            { trace += '['; }
    auto EndArray() -> void              { trace += ']'; }
    auto Flush() -> void                 {}
  };

  // Same contract, plus a native shortcut for SpecialType only.
  struct NativeTraceWriter : TraceWriter
  {
    auto WriteNative(SpecialType const&) -> void { trace += 'N'; }
  };

// 
  struct NativeReader
  {
    explicit NativeReader(SpecialType value) : _value{ value } {}

    template <typename T> auto Read() -> T { return T{}; }
    [[nodiscard]] auto IsNull() const -> bool { return false; }
    auto Subtree() const -> int { return 0; }
    auto EnterObject() -> void {}
    [[nodiscard]] auto HasField(std::string_view) const -> bool { return false; }
    [[nodiscard]] auto FieldNames() const -> std::vector<std::string> { return {}; }
    auto EnterField(std::string_view) -> void {}
    auto LeaveField() -> void {}
    auto LeaveObject() -> void {}
    auto EnterArray() -> void {}
    [[nodiscard]] auto HasNext() const -> bool { return false; }
    auto EnterNext() -> void {}
    auto LeaveNext() -> void {}
    auto LeaveArray() -> void {}
    [[nodiscard]] auto Path() const -> std::filesystem::path const& { return _path; }

    template <typename T>
      requires std::same_as<T, SpecialType>
    auto ReadNative() -> SpecialType { return _value; }

  private:
    SpecialType           _value;
    std::filesystem::path _path;
  };
}

static_assert(oxbox::serialization::WriterBackend<TraceWriter>);
static_assert(oxbox::serialization::WriterBackend<NativeTraceWriter>);
static_assert(oxbox::serialization::ReaderBackend<NativeReader>);
static_assert(oxbox::serialization::WriteNativeCapable<SpecialType, NativeTraceWriter>);
static_assert(!oxbox::serialization::WriteNativeCapable<SpecialType, TraceWriter>);
static_assert(!oxbox::serialization::WriteNativeCapable<std::int64_t, NativeTraceWriter>);
static_assert(oxbox::serialization::ReadNativeCapable<SpecialType, NativeReader>);
static_assert(!oxbox::serialization::ReadNativeCapable<std::int64_t, NativeReader>);

// A capable format claims the type and writes it in one native op.
TEST(NativeShortcut, CapableFormatWritesTheTypeNatively)
{
  NativeTraceWriter writer;
  oxbox::serialization::detail::WriteWalker walker{ writer };
  Wrapper const obj{ SpecialType{ 1, 2 }, 9 };
  walker(obj);
  EXPECT_EQ(writer.trace, "{fNfi}");
}

// A bare format has no shortcut, so the same type decomposes via its scheme.
TEST(NativeShortcut, BareFormatGracefullyDecomposesTheSameType)
{
  TraceWriter writer;
  oxbox::serialization::detail::WriteWalker walker{ writer };
  Wrapper const obj{ SpecialType{ 1, 2 }, 9 };
  walker(obj);
  EXPECT_EQ(writer.trace, "{f{fifi}fi}");
}

TEST(NativeShortcut, CapableFormatReadsTheTypeNatively)
{
  NativeReader reader{ SpecialType{ 7, 8 } };
  oxbox::serialization::detail::ReadWalker walker{ reader };
  SpecialType out{};
  walker(out);
  EXPECT_EQ(out, (SpecialType{ 7, 8 }));
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
