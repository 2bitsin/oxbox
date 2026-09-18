#pragma once

// The opt-in points a user type may declare: the canned-subtree marker and
// the archive/restore lifecycle calls.

#include <type_traits>

#include "oxbox/serialization/concepts.hpp"

namespace oxbox::serialization
{
  template <typename T> struct IsCannedT : std::false_type {};
  template <typename T> inline constexpr bool IsCanned = IsCannedT<T>::value; // NOLINT(readability-identifier-naming)

  template <typename T>
  constexpr auto RunArchiveHook(T& obj) -> void {
    if      constexpr (HasMemberArchive<T>) obj._Archive();
    else if constexpr (HasAdlArchive<T>)    _Archive(obj);
  }

  template <typename T>
  constexpr auto RunRestoreHook(T& obj) -> void {
    if      constexpr (HasMemberRestore<T>) obj._Restore();
    else if constexpr (HasAdlRestore<T>)    _Restore(obj);
  }
}
