#pragma once

// How a type says what it serializes as: `reflect_scheme(T*)`, found by ADL,
// which the tier below turns into an ordinary `Scheme{ Field... }`.

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <_buildutil/reflect.hpp>

#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/scheme.hpp"

namespace oxbox::serialization
{
  // declared here, defined below the tier it delegates to, to close the circle
  template <typename T>
  constexpr auto SchemeFor(T const& obj);
}

namespace oxbox::serialization::detail
{
  // A base's Field stays a Field<Base, M> inside a Scheme<Derived, ...>.
  template <typename Derived, typename... Tuples>
  constexpr auto MakeSchemeFromTuples(Tuples const&... ts) {
    auto cat = std::tuple_cat(ts...);
    return std::apply([](auto const&... fs) {
      return Scheme<Derived, std::remove_cvref_t<decltype(fs)>...>{fs...};
    }, cat);
  }

  // ENCAPSULATED is ignored: serialized state is the object's state, private
  // members included, which is what the friend tag granted.

  // A reference member has no pointer-to-member ([dcl.mptr]/3), so no Field.

  template <std::size_t INDEX, typename Reflected>
  constexpr auto ReflectedFieldTuple(Reflected scheme)
  {
    using Item = decltype(::reflect::scheme_item<INDEX>(scheme));
    if constexpr (_IS_POINTER_ITEM<Item>)
      return std::tuple{ Field{ Item::NAME_STRING, Item::REFERENCE,
                                Item::COMMENT, Item::LABEL } };
    else
      return std::tuple{ };
  }

  template <typename T, std::size_t... INDEX>
  constexpr auto ReflectedOwnFields(std::index_sequence<INDEX...>)
  {
    constexpr auto SCHEME{ ::reflect::scheme_of<T>() };
    return std::tuple_cat(ReflectedFieldTuple<INDEX>(SCHEME)...);
  }

  // declared before use: BaseFields calls back into it, reaching a grandbase
  template <typename T>
  constexpr auto ReflectedFieldsOf(T const& obj);

  // An untagged base with real fields does not serialize, and nothing says so.
  template <typename Base, typename T>
  constexpr auto BaseFields(T const& obj)
  {
    if constexpr (HasReflectedScheme<Base>)
      return ReflectedFieldsOf<Base>(static_cast<Base const&>(obj));
    else
      return std::tuple{ };
  }

  // in declaration order: base_list preserves it and this preserves base_list's
  template <typename Bases, typename T, std::size_t... INDEX>
  constexpr auto ReflectedBaseFields(T const& obj, std::index_sequence<INDEX...>)
  {
    return std::tuple_cat(
      BaseFields<::reflect::base_at<INDEX, Bases>>(obj)...);
  }

  // bases first: the wire order for any format that keeps declaration order
  template <typename T>
  constexpr auto ReflectedFieldsOf(T const& obj)
  {
    constexpr auto SCHEME{ ::reflect::scheme_of<T>() };
    using Bases = decltype(::reflect::bases_of<T>());
    return std::tuple_cat(
      ReflectedBaseFields<Bases>(
        obj, std::make_index_sequence<::reflect::bases_size(Bases{ })>{ }),
      ReflectedOwnFields<T>(
        std::make_index_sequence<::reflect::scheme_size(SCHEME)>{ }));
  }

  template <typename T>
  constexpr auto ReflectedSchemeOf(T const& obj)
  {
    static_assert(std::tuple_size_v<decltype(ReflectedFieldsOf(obj))> > 0,
      "a reflected type with no fields has no scheme to build: a Scheme is "
      "deduced from its fields and there are none. Counting: the type's own "
      "members, minus any that are references (those never serialize), plus "
      "whatever the bases in its base_list contribute -- a base with no "
      "scheme contributes nothing. Give the type a member, or tag the base "
      "that was meant to supply them.");
    return MakeSchemeFromTuples<T>(ReflectedFieldsOf(obj));
  }

  // the wire spelling is the LABEL or the identifier verbatim, never derived
  template <std::size_t INDEX, typename E, typename Reflected>
  consteval auto ReflectedEnumEntry(Reflected scheme)
    -> std::pair<E, std::string_view>
  {
    using Item = decltype(::reflect::scheme_item<INDEX>(scheme));
    return { Item::VALUE,
             Item::LABEL.empty() ? Item::NAME_STRING : Item::LABEL };
  }

  // consteval because EnumMap's constructor is, and its array must be built
  // inside an immediate function context
  template <typename E, std::size_t... INDEX>
  consteval auto ReflectedEnumMapFrom(std::index_sequence<INDEX...>)
  {
    constexpr auto SCHEME{ ::reflect::scheme_of<E>() };
    std::pair<E, std::string_view> entries[]{
      ReflectedEnumEntry<INDEX, E>(SCHEME)... };
    return EnumMap{ entries };
  }

  template <typename E>
  consteval auto ReflectedEnumMapOf()
  {
    constexpr auto SCHEME{ ::reflect::scheme_of<E>() };
    static_assert(::reflect::scheme_size(SCHEME) > 0,
      "a reflected enum with no enumerators has no wire mapping to build");
    return ReflectedEnumMapFrom<E>(
      std::make_index_sequence<::reflect::scheme_size(SCHEME)>{ });
  }
}

namespace oxbox::serialization
{
  // nothing reads the object's value, so this stays a constant expression
  template <typename T>
  constexpr auto SchemeFor(T const& obj) {
    return detail::ReflectedSchemeOf(obj);
  }

  template <typename T>
    requires HasEnumMap<T>
  constexpr auto EnumMapFor() {
    return detail::ReflectedEnumMapOf<T>();
  }
}
