#include "read-walker.stub.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

auto Accepted() -> void
{
  std::map<std::uint16_t, std::int32_t>          by_number;
  std::vector<std::pair<std::uint32_t, bool>>    pairs;
  std::map<std::string, std::int32_t>            by_name;
  ReadInto(by_number);
  ReadInto(pairs);
  ReadInto(by_name);
}
