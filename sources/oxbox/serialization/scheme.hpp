// NOLINTBEGIN(misc-non-private-member-variables-in-classes): the serialization DSL is a
// value/descriptor surface by design (module-wide ruling)
#pragma once

// A field, the list a type serializes as, and an enum's name table.

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace oxbox::serialization
{
  template <typename>   struct IsStdOptionalT                   : std::false_type {};
  template <typename U> struct IsStdOptionalT<std::optional<U>> : std::true_type {};

  template <typename T>
  concept FixedSequence =
       requires(T const& c) { c.begin(); c.end(); typename T::value_type; }
    && requires { std::tuple_size<std::remove_cvref_t<T>>::value; }
    && (!requires(T& c, typename T::value_type v) { c.push_back(std::move(v)); });

  template <typename T, typename M>
  struct Field {
    using owner      = T;
    using value_type = M;
    std::string_view name;       // the member's own spelling (#m)
    M T::*           ptr;
    // never on the wire and never read by the walkers
    std::string_view description;
    // empty leaves a format free to derive a spelling its own way
    std::string_view spelling;

    consteval Field(std::string_view n, M T::* p, std::string_view d = {},
                    std::string_view as = {}) noexcept
    : name        { n }
    , ptr         { p }
    , description { d }
    , spelling    { as }
    {}

    [[nodiscard]] constexpr auto WireName() const -> std::string_view
    { return spelling.empty() ? name : spelling; }

    constexpr auto Get(T const& obj) const -> M const& { return obj.*ptr; }
    constexpr auto Set(T& obj, M value) const -> void  { obj.*ptr = std::move(value); }
    // the mutable slot a dotted-path write descends through
    constexpr auto Ref(T& obj) const -> M& { return obj.*ptr; }
  };

  template <typename T, typename... Fs>
  struct Scheme {
    std::tuple<Fs...> fields;
    using owner = T;

    // constexpr and not consteval: MSVC's consteval evaluator trips C1054
    // (initializers nested too deeply) building the tuple past ~40 fields
    constexpr Scheme(Fs... fs) noexcept : fields{fs...} {}
  };

  template <typename T, typename M, typename... Rest>
  Scheme(Field<T, M>, Rest...) -> Scheme<T, Field<T, M>, Rest...>;

  template <typename E, std::size_t N>
  struct EnumMap {
    std::array<std::pair<E, std::string_view>, N> entries;

    consteval EnumMap(std::pair<E, std::string_view> const (&arr)[N]) noexcept {
      for (std::size_t i = 0; i < N; ++i) entries[i] = arr[i];
    }

    [[nodiscard]] constexpr auto ToString(E v) const
      -> std::optional<std::string_view>
    {
      for (auto const& [k, s] : entries) if (k == v) return s;
      return std::nullopt;
    }

    [[nodiscard]] constexpr auto FromString(std::string_view s) const
      -> std::optional<E>
    {
      for (auto const& [k, name] : entries) if (name == s) return k;
      return std::nullopt;
    }
  };

  template <typename E, std::size_t N>
  EnumMap(std::pair<E, std::string_view> const (&)[N]) -> EnumMap<E, N>;
}

// NOLINTEND(misc-non-private-member-variables-in-classes)
