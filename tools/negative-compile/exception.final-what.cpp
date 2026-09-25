// Refused on purpose: the held text needs what() for itself, and this base seals it.
#include "oxbox/utilities/exception.hpp"

#include "oxbox/utilities/hash.hpp"

#include <exception>

using namespace oxbox::utilities::literals;

class Sealed : public std::exception
{
public:
  [[nodiscard]] auto what() const noexcept -> char const* final { return "sealed"; }
};

using Refused = oxbox::utilities::Exception<"Refused"_hash, Sealed, "port {}", int>;

auto main() -> int { return Refused{ 3 }.what()[0]; }
