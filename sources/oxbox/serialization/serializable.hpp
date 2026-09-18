#pragma once

// The module's entry point: the primary Serialize / Deserialize pair, over
// the vocabulary, hooks, reflected tier and walkers included below.

#include <type_traits>

#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/hooks.hpp"
#include "oxbox/serialization/read-walker.hpp"
#include "oxbox/serialization/reflected-scheme.hpp"
#include "oxbox/serialization/scheme.hpp"
#include "oxbox/serialization/write-walker.hpp"

namespace oxbox::serialization
{
  template <typename F, typename T, Sink S>
  auto Serialize(T& obj, S& sink) -> void {
    if constexpr (HasArchiveHook<T>) RunArchiveHook(obj);
    typename F::template Writer<S> w{sink};
    detail::WriteWalker walker{w};
    walker(obj);
    w.Flush();
  }

  template <typename F, typename T, Sink S>
    requires (!HasArchiveHook<T>)
  auto Serialize(T const& obj, S& sink) -> void {
    typename F::template Writer<S> w{sink};
    detail::WriteWalker walker{w};
    walker(obj);
    w.Flush();
  }

  template <typename F, typename T, Source Src>
  auto Deserialize(Src& source) -> T {
    static_assert(std::is_default_constructible_v<T>,
      "Deserialize<F, T> needs a default-constructible T: the parse fills "
      "a default-constructed object field by field.");
    typename F::template Reader<Src> r{source};
    T out{};
    detail::ReadWalker walker{r};
    walker(out);
    // a backend may audit the parse as a whole (a command line's unknown-option
    // check) only once the walker, including any _Restore re-reads, has settled
    if constexpr (requires { r.Finish(); }) r.Finish();
    if constexpr (HasRestoreHook<T>) RunRestoreHook(out);
    return out;
  }
}
