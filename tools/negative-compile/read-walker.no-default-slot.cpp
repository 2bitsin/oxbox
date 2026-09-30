#include "read-walker.stub.hpp"

#include <cstdint>
#include <map>

struct NoDefault
{
  explicit NoDefault(std::int32_t value) : value{ value } {}
  std::int32_t value;
};

auto Refused() -> void
{
  std::map<std::uint16_t, NoDefault> out;
  ReadInto(out);
}
