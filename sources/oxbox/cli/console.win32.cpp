#include "oxbox/cli/console.hpp"

#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace oxbox::cli::detail::console
{
  auto AttachParentConsole() noexcept -> bool
  {
    if (!::AttachConsole(ATTACH_PARENT_PROCESS))
      return false;

    // CONOUT$ and not "con": the CRT's stdout was bound to nothing when the
    // process started, and attaching a console does not rebind it.
    std::FILE* reopened{ nullptr };
    static_cast<void>(::freopen_s(&reopened, "CONOUT$", "w", stdout));
    static_cast<void>(::freopen_s(&reopened, "CONOUT$", "w", stderr));
    return true;
  }
}
