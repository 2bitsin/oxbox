#pragma once
// Chunk: std::views::chunk where <version> says the library has it, and
// consecutive subranges over a sized random-access range where it does not.

#include "oxbox/utilities/exception.hpp"
#include "oxbox/utilities/hash.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <version>

namespace oxbox::utilities::detail::chunk
{
  using ChunkSizeZero = Exception<"ChunkSizeZero"_hash, std::invalid_argument, "a chunk holds at least one element">;

  // What the fallback can split, refused alike on every library so that no
  // call compiles on one and not on another.
  template <typename _Range>
  concept Chunkable = std::ranges::viewable_range<_Range>
                   && std::ranges::random_access_range<std::views::all_t<_Range>>
                   && std::ranges::sized_range<std::views::all_t<_Range>>;

  template <typename _Chunker, Chunkable _Range>
  constexpr auto Chunked(_Range&& range, std::size_t size)
  {
    if (size == 0) { throw ChunkSizeZero{ }; }
    return _Chunker{ }(std::forward<_Range>(range), size);
  }

  template <typename _Chunker>
  class ChunkClosure : public std::ranges::range_adaptor_closure<ChunkClosure<_Chunker>>
  {
  public:
    constexpr explicit ChunkClosure(std::size_t size) noexcept
    : _size{ size }
    { }

    template <Chunkable _Range>
    constexpr auto operator()(_Range&& range) const
    { return Chunked<_Chunker>(std::forward<_Range>(range), _size); }

  private:
    std::size_t _size;
  };

  template <typename _Chunker>
  struct ChunkAdaptor
  {
    template <Chunkable _Range>
    constexpr auto operator()(_Range&& range, std::size_t size) const
    { return Chunked<_Chunker>(std::forward<_Range>(range), size); }

    constexpr auto operator()(std::size_t size) const noexcept -> ChunkClosure<_Chunker>
    { return ChunkClosure<_Chunker>{ size }; }
  };
}

namespace oxbox::utilities::detail::chunk::fallback
{
  template <std::ranges::view _View>
  class ChunkAt
  {
  public:
    constexpr ChunkAt(_View view, std::size_t size)
    : _view{ std::move(view) }, _size{ size }
    { }

    constexpr auto operator()(std::size_t index) -> std::ranges::subrange<std::ranges::iterator_t<_View>>
    { return Slice(_view, index); }

    constexpr auto operator()(std::size_t index) const -> std::ranges::subrange<std::ranges::iterator_t<_View const>>
      requires std::ranges::random_access_range<_View const> && std::ranges::sized_range<_View const>
    { return Slice(_view, index); }

  private:
    template <typename _Base>
    constexpr auto Slice(_Base& base, std::size_t index) const -> std::ranges::subrange<std::ranges::iterator_t<_Base>>
    {
      using Difference   = std::ranges::range_difference_t<_Base>;
      auto const first   = index * _size;
      auto const last    = (std::min)(first + _size, static_cast<std::size_t>(std::ranges::size(base)));
      auto const origin  = std::ranges::begin(base);
      return { origin + static_cast<Difference>(first), origin + static_cast<Difference>(last) };
    }

    _View       _view;
    std::size_t _size;
  };

  struct Chunker
  {
    template <Chunkable _Range>
    constexpr auto operator()(_Range&& range, std::size_t size) const
    {
      auto view        = std::views::all(std::forward<_Range>(range));
      auto const total = static_cast<std::size_t>(std::ranges::size(view));
      auto const count = total / size + (total % size == 0 ? 0 : 1);
      return std::views::iota(std::size_t{ 0 }, count)
           | std::views::transform(ChunkAt{ std::move(view), size });
    }
  };

  inline constexpr ChunkAdaptor<Chunker> Chunk{ };
}

namespace oxbox::utilities::detail::chunk
{
#ifdef __cpp_lib_ranges_chunk
  struct Chunker
  {
    template <Chunkable _Range>
    constexpr auto operator()(_Range&& range, std::size_t size) const
    {
      return std::views::chunk(std::forward<_Range>(range),
                               static_cast<std::ranges::range_difference_t<_Range>>(size));
    }
  };
#else
  using fallback::Chunker;
#endif

  inline constexpr ChunkAdaptor<Chunker> Chunk{ };
}

namespace oxbox::utilities
{
  using detail::chunk::Chunk;
  using detail::chunk::ChunkSizeZero;
}
