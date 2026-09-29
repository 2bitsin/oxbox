// The control, and it must compile: both told, and the condition names an identifier nothing declares.
#define OXBOX_CONTRACT_MODE ::oxbox::platform::ContractMode::OFF
#define OXBOX_CONTRACTS_IGNORE
#include "oxbox/platform/contract.hpp"

auto main() -> int
{
  OXBOX_EXPECTS(NeverDeclared(), "the condition is not in the build");
  OXBOX_ENSURES(NeverDeclared(), "the condition is not in the build");
  return 0;
}
