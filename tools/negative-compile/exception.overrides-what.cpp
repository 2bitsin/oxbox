// The control, and it must compile: the base's what() is open, so the held text takes it over.
#include "oxbox/utilities/exception.hpp"

#include "oxbox/utilities/hash.hpp"

#include <exception>

using namespace oxbox::utilities::literals;

class Open : public std::exception
{
public:
  [[nodiscard]] auto what() const noexcept -> char const* override { return "open"; }
};

using Held = oxbox::utilities::Exception<"Held"_hash, Open, "port {}", int>;

auto main() -> int { return Held{ 3 }.what()[0] == 'p' ? 0 : 1; }
