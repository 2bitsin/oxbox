#pragma once
// Present: the contained values of a range's engaged optionals, in order;
// std::views::join where <version> says optional is a range (P3168).

#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <version>

namespace oxbox::utilities::detail::present
{
  template <typename _Type>
  inline constexpr bool IS_OPTIONAL{ false };

  template <typename _Type>
  inline constexpr bool IS_OPTIONAL<std::optional<_Type>>{ true };

  // Optionals only, on every library: join takes any range of ranges.
  template <typename _Range>
  concept OptionalRange = std::ranges::viewable_range<_Range> && std::ranges::input_range<_Range>
                       && IS_OPTIONAL<std::remove_cvref_t<std::ranges::range_reference_t<_Range>>>;

  template <typename _Presenter>
  struct PresentAdaptor : std::ranges::range_adaptor_closure<PresentAdaptor<_Presenter>>
  {
    template <OptionalRange _Range>
    constexpr auto operator()(_Range&& range) const
    { return _Presenter{ }(std::forward<_Range>(range)); }
  };
}

namespace oxbox::utilities::detail::present::fallback
{
  inline constexpr auto HasValue = []<typename _Optional>(_Optional const& optional) -> bool {
    return optional.has_value();
  };

  inline constexpr auto Referenced = []<typename _Optional>(_Optional&& optional) -> decltype(*optional) {
    return *optional;
  };

  // A prvalue element dies with the step, so only its value can outlive it.
  inline constexpr auto Released = []<typename _Optional>(_Optional&& optional)
    -> typename std::remove_cvref_t<_Optional>::value_type {
    return std::move(*optional);
  };

  struct Presenter
  {
    template <OptionalRange _Range>
    constexpr auto operator()(_Range&& range) const
    {
      auto engaged = std::forward<_Range>(range) | std::views::filter(HasValue);
      if constexpr (std::is_reference_v<std::ranges::range_reference_t<_Range>>)
        return std::move(engaged) | std::views::transform(Referenced);
      else
        return std::move(engaged) | std::views::transform(Released);
    }
  };

  inline constexpr PresentAdaptor<Presenter> Present{ };
}

namespace oxbox::utilities::detail::present
{
#ifdef __cpp_lib_optional_range_support
  struct Presenter
  {
    template <OptionalRange _Range>
    constexpr auto operator()(_Range&& range) const
    { return std::views::join(std::forward<_Range>(range)); }
  };
#else
  using fallback::Presenter;
#endif

  inline constexpr PresentAdaptor<Presenter> Present{ };
}

namespace oxbox::utilities
{
  using detail::present::Present;
}
