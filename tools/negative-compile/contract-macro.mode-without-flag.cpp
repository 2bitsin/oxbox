// The mode says OFF and the preprocessor was not told: the conditions would stay in the build.
#define OXBOX_CONTRACT_MODE ::oxbox::platform::ContractMode::OFF
#include "oxbox/platform/contract.hpp"

auto main() -> int
{
  OXBOX_EXPECTS(true, "the mode and the flag agree");
  return 0;
}
