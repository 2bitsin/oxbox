#pragma once
// Enumerate: std::views::enumerate where <version> says the library has it,
// and the same (index, element) tuples zipped from an iota where it does not.

#include <ranges>
#include <utility>
#include <version>

namespace oxbox::utilities::detail::enumerate
{
  template <typename _Range>
  concept Enumerable = std::ranges::viewable_range<_Range> && std::ranges::input_range<_Range>;

  template <typename _Enumerator>
  struct EnumerateAdaptor : std::ranges::range_adaptor_closure<EnumerateAdaptor<_Enumerator>>
  {
    template <Enumerable _Range>
    constexpr auto operator()(_Range&& range) const
    { return _Enumerator{ }(std::forward<_Range>(range)); }
  };
}

namespace oxbox::utilities::detail::enumerate::fallback
{
  struct Enumerator
  {
    // A bounded iota keeps the zip sized when the range is; zip is common only
    // when the range is also random-access, where views::enumerate needs forward.
    template <Enumerable _Range>
    constexpr auto operator()(_Range&& range) const
    {
      using Index = std::ranges::range_difference_t<_Range>;
      auto view   = std::views::all(std::forward<_Range>(range));
      if constexpr (std::ranges::sized_range<decltype(view)>) {
        auto const count{ static_cast<Index>(std::ranges::size(view)) };
        return std::views::zip(std::views::iota(Index{ 0 }, count), std::move(view));
      }
      else {
        return std::views::zip(std::views::iota(Index{ 0 }), std::move(view));
      }
    }
  };

  inline constexpr EnumerateAdaptor<Enumerator> Enumerate{ };
}

namespace oxbox::utilities::detail::enumerate
{
#ifdef __cpp_lib_ranges_enumerate
  struct Enumerator
  {
    template <Enumerable _Range>
    constexpr auto operator()(_Range&& range) const
    { return std::views::enumerate(std::forward<_Range>(range)); }
  };
#else
  using fallback::Enumerator;
#endif

  inline constexpr EnumerateAdaptor<Enumerator> Enumerate{ };
}

namespace oxbox::utilities
{
  using detail::enumerate::Enumerate;
}
