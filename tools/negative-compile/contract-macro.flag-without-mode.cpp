// The preprocessor was told to ignore and the mode still says STOP: every check would vanish.
#define OXBOX_CONTRACT_MODE ::oxbox::platform::ContractMode::STOP
#define OXBOX_CONTRACTS_IGNORE
#include "oxbox/platform/contract.hpp"

auto main() -> int
{
  OXBOX_EXPECTS(true, "the mode and the flag agree");
  return 0;
}
