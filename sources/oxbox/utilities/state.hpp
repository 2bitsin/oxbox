#pragma once
// What the tools accumulate across a run: shared as a shared_ptr rather than
// a member reference, so it survives the session object being moved.

#include <cstdint>
#include <string>
#include <vector>

namespace oxbox::utilities::detail::state
{
  struct Annotation {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::string label;
    std::string text;
  };

  struct SessionState {
    std::vector<Annotation> annotations;
    std::string report;
    bool remember = false;  // --remember: mirror annotations into memory
  };

}

namespace oxbox::utilities
{
  using detail::state::Annotation;
  using detail::state::SessionState;
}
