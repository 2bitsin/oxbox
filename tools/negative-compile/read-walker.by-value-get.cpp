#include "read-walker.stub.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

struct ByValue
{
  template <std::size_t I>
  auto get() const -> std::int32_t { return 42; }
};

template <> struct std::tuple_size<ByValue>       : std::integral_constant<std::size_t, 1> {};
template <> struct std::tuple_element<0, ByValue> { using type = std::int32_t; };

auto Refused() -> void
{
  ByValue out;
  ReadInto(out);
}
