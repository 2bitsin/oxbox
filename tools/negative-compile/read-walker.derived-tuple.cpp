#include "read-walker.stub.hpp"

#include <cstdint>
#include <string>
#include <tuple>

struct Row : std::tuple<std::int32_t, std::string> {};

auto Refused() -> void
{
  Row out;
  ReadInto(out);
}
