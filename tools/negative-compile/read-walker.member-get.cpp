#include "read-walker.stub.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

struct Mutable
{
  std::int32_t value{};

  template <std::size_t I> auto get() const -> std::int32_t const& { return value; }
  template <std::size_t I> auto get()       -> std::int32_t&       { return value; }
};

template <> struct std::tuple_size<Mutable>       : std::integral_constant<std::size_t, 1> {};
template <> struct std::tuple_element<0, Mutable> { using type = std::int32_t; };

auto Accepted() -> void
{
  Mutable out;
  ReadInto(out);
}
