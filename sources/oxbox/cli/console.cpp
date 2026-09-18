#include "oxbox/cli/console.hpp"

#include <cstdio>

namespace oxbox::cli::detail::console
{
  auto SetStdoutUnbuffered() noexcept -> void
  {
    // _IONBF, not _IOLBF: the Microsoft CRT kills the process on size < 2.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
  }

  // Attach first: reopening the streams onto a console discards their mode.
  auto PrepareConsole() noexcept -> void
  {
    static_cast<void>(AttachParentConsole());
    SetStdoutUnbuffered();
  }
}
