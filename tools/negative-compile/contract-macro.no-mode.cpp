// A macro used with no OXBOX_CONTRACT_MODE defined: refused by name, never a quiet default.
#include "oxbox/platform/contract.hpp"

auto main() -> int
{
  OXBOX_EXPECTS(true, "the mode is named");
  return 0;
}
