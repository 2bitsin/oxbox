#include "oxbox/utilities/map-like.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using oxbox::utilities::MapLike;

static_assert( MapLike<std::map<std::string, std::int32_t>>);
static_assert( MapLike<std::multimap<std::int32_t, std::string>>);
static_assert( MapLike<std::unordered_map<std::uint16_t, bool>>);
static_assert(!MapLike<std::set<std::int32_t>>);
static_assert(!MapLike<std::vector<std::int32_t>>);
static_assert(!MapLike<std::int32_t>);
