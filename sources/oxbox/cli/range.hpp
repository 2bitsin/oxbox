#pragma once

// An input view whose source is erased and whose value type is not, so a
// command's operator() stays a non-template function. Stands in for
// std::ranges::any_view / any_iterator (P3411) until a library ships it.

#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <utility>

namespace oxbox::cli::detail::range
{
  namespace stdr = std::ranges;

  // constructible_from, not convertible_to: string_view to string is explicit.
  template <typename Iterator, typename Sentinel, typename Type>
  concept ErasableSource =
    std::input_iterator<Iterator> &&
    std::sentinel_for<Sentinel, Iterator> &&
    std::copyable<Iterator> &&
    std::copyable<Sentinel> &&
    std::constructible_from<Type, std::iter_reference_t<Iterator>>;

  template <typename Type>
  class AnyIterator
  {
  private:
    // The cursor carries its own end: a default AnyIterator is the sentinel.
    struct Cursor
    {
      virtual ~Cursor() = default;
      virtual auto Clone() const -> std::unique_ptr<Cursor> = 0;
      virtual auto Read() const -> Type = 0;
      virtual auto Step() -> void = 0;
      virtual auto Exhausted() const -> bool = 0;
      virtual auto SameAs(Cursor const& other) const -> bool = 0;
    };

    template <typename Iterator, typename Sentinel>
      requires ErasableSource<Iterator, Sentinel, Type>
    class Position final : public Cursor
    {
    public:
      Position(Iterator first, Sentinel last)
        : _current{ std::move(first) }, _last{ std::move(last) }
      { }

      auto Clone() const -> std::unique_ptr<Cursor> override
      {
        return std::make_unique<Position>(*this);
      }

      auto Read() const -> Type override
      {
        return static_cast<Type>(*_current);
      }

      auto Step() -> void override { ++_current; }

      auto Exhausted() const -> bool override { return _current == _last; }

      // A single-pass source cannot compare iterators; identity is all left.
      auto SameAs(Cursor const& other) const -> bool override
      {
        auto const* twin{ dynamic_cast<Position const*>(&other) };
        if (twin == nullptr)
          return false;
        if constexpr (std::equality_comparable<Iterator>)
          return _current == twin->_current;
        else
          return this == twin;
      }

    private:
      Iterator _current;
      Sentinel _last;
    };

  public:
    using value_type = Type;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;

    AnyIterator() = default;

    template <typename Iterator, typename Sentinel>
      requires ErasableSource<Iterator, Sentinel, Type>
    AnyIterator(Iterator first, Sentinel last)
      : _cursor{ std::make_unique<Position<Iterator, Sentinel>>(
          std::move(first), std::move(last)) }
    { }

    AnyIterator(AnyIterator const& other)
      : _cursor{ other._cursor ? other._cursor->Clone() : nullptr }
    { }

    auto operator = (AnyIterator const& other) -> AnyIterator&
    {
      if (this != &other)
        _cursor = other._cursor ? other._cursor->Clone() : nullptr;
      return *this;
    }

    AnyIterator(AnyIterator&&) noexcept = default;
    auto operator = (AnyIterator&&) noexcept -> AnyIterator& = default;
    ~AnyIterator() = default;

    // Reading past the end is the caller's error; advancing the end is not.
    auto operator * () const -> Type { return _cursor->Read(); }

    auto operator ++ () -> AnyIterator&
    {
      if (_cursor)
        _cursor->Step();
      return *this;
    }

    // Void, not a copy: a copy would snapshot a single-pass source.
    auto operator ++ (int) -> void { ++*this; }

    friend auto operator == (AnyIterator const& lhs,
                             AnyIterator const& rhs) -> bool
    {
      auto const lhs_spent{ lhs.Spent() };
      auto const rhs_spent{ rhs.Spent() };
      if (lhs_spent || rhs_spent)
        return lhs_spent == rhs_spent;
      return lhs._cursor->SameAs(*rhs._cursor);
    }

  private:
    auto Spent() const -> bool
    {
      return _cursor == nullptr || _cursor->Exhausted();
    }

    std::unique_ptr<Cursor> _cursor { };
  };

  template <typename Type>
  using AnyView = stdr::subrange<AnyIterator<Type>, AnyIterator<Type>>;

  template <typename Type>
  using RangeView = AnyView<Type>;

  // Borrows: valid only while the source range and its elements stay alive.
  template <typename Type, stdr::input_range InputRange>
    requires stdr::borrowed_range<InputRange> &&
             ErasableSource<stdr::iterator_t<InputRange>,
                            stdr::sentinel_t<InputRange>, Type>
  auto EraseRange(InputRange&& input_range) -> AnyView<Type>
  {
    return AnyView<Type>{
      AnyIterator<Type>{ stdr::begin(input_range), stdr::end(input_range) },
      AnyIterator<Type>{ }
    };
  }

  template <typename Type, typename Iterator, typename Sentinel>
    requires ErasableSource<Iterator, Sentinel, Type>
  auto EraseRange(Iterator first, Sentinel last) -> AnyView<Type>
  {
    return AnyView<Type>{
      AnyIterator<Type>{ std::move(first), std::move(last) },
      AnyIterator<Type>{ }
    };
  }
}

namespace oxbox::cli
{
  using detail::range::AnyIterator;
  using detail::range::AnyView;
  using detail::range::ErasableSource;
  using detail::range::EraseRange;
  using detail::range::RangeView;
}
