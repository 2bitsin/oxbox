#include "read-walker.stub.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

struct ConstRef
{
  std::int32_t value{};

  template <std::size_t I>
  auto get() const -> std::int32_t const& { return value; }
};

template <> struct std::tuple_size<ConstRef>       : std::integral_constant<std::size_t, 1> {};
template <> struct std::tuple_element<0, ConstRef> { using type = std::int32_t; };

auto Refused() -> void
{
  ConstRef out;
  ReadInto(out);
}
