#pragma once

// What shape a member's type has: how many command-line tokens it takes,
// and how a value is put into it.

#include <concepts>
#include <cstddef>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace oxbox::cli::detail::member_shape
{
  inline constexpr std::size_t UNBOUNDED{
    std::numeric_limits<std::size_t>::max() };

  // How many values the member holds, from its type alone.
  template <typename Type>
  consteval auto ArityOf() -> std::size_t
  {
    using Bare = std::remove_cvref_t<Type>;
    if constexpr (requires { std::tuple_size<Bare>::value; })
      return std::tuple_size_v<Bare>;                 // array, tuple: exactly N
    else if constexpr (requires(Bare& c, typename Bare::value_type v) {
                         c.begin(); c.end(); c.insert(c.end(), v); }
                       && !std::same_as<Bare, std::string>)
      return UNBOUNDED;                               // vector, list, set: any
    else
      return 1u;                                      // scalar, optional, enum
  }

  // std::set has a key_type too, hence the mapped_type requirement.
  template <typename Type>
  concept MapLike = ArityOf<Type>() == UNBOUNDED
    && requires { typename std::remove_cvref_t<Type>::key_type;
                  typename std::remove_cvref_t<Type>::mapped_type; };

  template <typename Type>
  concept ListLike = ArityOf<Type>() == UNBOUNDED && !MapLike<Type>;

  // A tuple has a size but no element type, so it falls to convert.hpp.
  template <typename Type>
  concept ArrayLike = requires(std::remove_cvref_t<Type>& slot) {
    typename std::remove_cvref_t<Type>::value_type;
    std::tuple_size<std::remove_cvref_t<Type>>::value;
    slot[0u];
  };

  template <typename Container, typename Value>
  auto Append(Container& into, Value value) -> void
  {
    if constexpr (requires { into.emplace_back(std::move(value)); })
      into.emplace_back(std::move(value));
    else if constexpr (requires { into.push_back(std::move(value)); })
      into.push_back(std::move(value));
    else if constexpr (requires { into.insert(into.end(), std::move(value)); })
      into.insert(into.end(), std::move(value));
    else if constexpr (requires { into.insert(std::move(value)); })
      into.insert(std::move(value));
    else
      static_assert(false, "this member's container cannot be appended to: "
        "it offers none of emplace_back, push_back, insert(where, value) "
        "or insert(value).");
  }
}

namespace oxbox::cli
{
  using detail::member_shape::ArityOf;
  using detail::member_shape::UNBOUNDED;
}
