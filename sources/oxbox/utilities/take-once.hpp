#pragma once
// A value held for exactly one reader: taking it empties the holder.

#include <concepts>
#include <utility>

namespace oxbox::utilities::detail::take_once
{
  // The default of `_Held` is the empty answer, and one thread joins both.
  template <typename _Held>
    requires std::default_initializable<_Held> && std::movable<_Held>
  class TakeOnce
  {
  public:
    auto Record(_Held value) -> void { _held = std::move(value); }

    [[nodiscard]] auto Take() -> _Held { return std::exchange(_held, _Held{ }); }

  private:
    _Held _held{ };
  };
}

namespace oxbox::utilities
{
  using detail::take_once::TakeOnce;
}
