#pragma once

#include "oxbox/utilities/short-types.hpp"
#include "oxbox/utilities/span.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

namespace oxbox::platform::detail::memory
{
  using namespace utilities;

  // every allocation satisfies at least this alignment (host pages are
  // never smaller; PageSize() reports the actual)
  inline constexpr std::size_t PAGE_ALIGNMENT{ 4096u };

  auto PageSize() noexcept -> std::size_t;

  // raw page-granular allocation: arrives zero-filled from the host,
  // released whole; failure to allocate throws precisely
  auto AllocatePages(std::size_t bytes) -> WritableBytes;
  auto ReleasePages(WritableBytes pages) noexcept -> void;

  // The owning page-aligned array, tailored to trivial payloads (all this
  // codebase stores in one): construction is a fill, destruction is a
  // release, copies are real allocations -- snapshot-friendly.
  template <typename _Type>
    requires (std::is_trivially_copyable_v<_Type>
           && std::is_trivially_default_constructible_v<_Type>)
  struct PageAlignedArray
  {
    PageAlignedArray() noexcept = default;

    explicit PageAlignedArray(std::size_t count)
    : _items{ Claim(count) }
    { }                                    // host pages arrive zeroed

    PageAlignedArray(std::size_t count, _Type fill)
    : _items{ Claim(count) }
    { std::ranges::fill(_items, fill); }

    PageAlignedArray(PageAlignedArray const& other)
    : _items{ Claim(other._items.size()) }
    { std::ranges::copy(other._items, _items.begin()); }

    PageAlignedArray(PageAlignedArray&& other) noexcept
    : _items{ std::exchange(other._items, { }) }
    { }

    auto operator = (PageAlignedArray const& other) -> PageAlignedArray&
    { PageAlignedArray copy{ other }; std::swap(_items, copy._items); return *this; }

    auto operator = (PageAlignedArray&& other) noexcept -> PageAlignedArray&
    { std::swap(_items, other._items); return *this; }

    ~PageAlignedArray() noexcept
    { if (!_items.empty()) ReleasePages(AsWritableBytes(_items)); }

    auto size () const noexcept -> std::size_t  { return _items.size();  }
    auto empty() const noexcept -> bool         { return _items.empty(); }
    auto data ()       noexcept -> _Type*       { return _items.data();  }
    auto data () const noexcept -> _Type const* { return _items.data();  }

    auto begin()       noexcept { return _items.begin(); }
    auto end  ()       noexcept { return _items.end();   }
    auto begin() const noexcept { return std::span<_Type const>{ _items }.begin(); }
    auto end  () const noexcept { return std::span<_Type const>{ _items }.end();   }

    auto operator [] (std::size_t index)       noexcept -> _Type&       { return _items[index]; }
    auto operator [] (std::size_t index) const noexcept -> _Type const& { return _items[index]; }

    operator std::span<_Type      > ()       noexcept { return _items; }
    operator std::span<_Type const> () const noexcept { return _items; }

  private:
    static auto Claim(std::size_t count) -> std::span<_Type>
    {
      if (count == 0u) return { };
      return SpanCast<_Type>(AllocatePages(count * sizeof(_Type)));
    }

    std::span<_Type> _items{ };
  };
}

namespace oxbox::platform
{
  using detail::memory::AllocatePages;
  using detail::memory::PAGE_ALIGNMENT;
  using detail::memory::PageAlignedArray;
  using detail::memory::PageSize;
  using detail::memory::ReleasePages;
}
