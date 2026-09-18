#include "oxbox/cli/console.hpp"

namespace oxbox::cli::detail::console
{
  // A process inherits its parent's descriptors, so there is nothing to do.
  auto AttachParentConsole() noexcept -> bool { return false; }
}
