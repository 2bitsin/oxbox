#include "read-walker.stub.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>

struct Row : std::tuple<std::int32_t, std::string> {};

template <> struct std::tuple_size<Row>       : std::integral_constant<std::size_t, 2> {};
template <> struct std::tuple_element<0, Row> { using type = std::int32_t; };
template <> struct std::tuple_element<1, Row> { using type = std::string; };

auto Accepted() -> void
{
  Row out;
  ReadInto(out);
}
