#pragma once

// The flattened member list the cli reads: bases first, recursively, then
// own, in declaration order -- serialization's ReflectedFieldsOf order, so
// the two modules agree. A base with no scheme contributes nothing, silently.

#include "oxbox/cli/command.hpp"

#include <_buildutil/reflect.hpp>

#include <concepts>
#include <cstddef>
#include <tuple>
#include <utility>

namespace oxbox::cli::detail::scheme
{
  // InlineSegment is the one type whose empty member list is deliberate.
  template <typename Owner>
  concept HasMemberList =
    ::reflect::reflected<Owner> || std::same_as<Owner, command::InlineSegment>;

  template <typename Owner>
  inline constexpr auto SCHEME_OF{ ::reflect::scheme_of<Owner>() };

  template <typename Owner> requires ::reflect::reflected<Owner>
  inline constexpr auto BASES_OF{ ::reflect::bases_of<Owner>() };

  template <typename ... LIST>
  using Concat = decltype(std::tuple_cat(std::declval<LIST>()...));

  template <typename Owner, typename INDICES> struct OwnItems;

  template <typename Owner, std::size_t ... INDEX>
  struct OwnItems<Owner, std::index_sequence<INDEX...>>
  {
    using type = std::tuple<
      decltype(::reflect::scheme_item<INDEX>(SCHEME_OF<Owner>))...>;
  };

  // Mutually recursive with BaseItems: that is how a grandbase is reached.
  template <typename Owner, bool = ::reflect::reflected<Owner>>
  struct Flattened;

  template <typename Base, bool = ::reflect::reflected<Base>>
  struct BaseItems
  {
    using type = std::tuple<>;
  };

  template <typename Base>
  struct BaseItems<Base, true>
  {
    using type = typename Flattened<Base>::type;
  };

  template <typename BASES, typename INDICES> struct BaseListItems;

  template <typename BASES, std::size_t ... INDEX>
  struct BaseListItems<BASES, std::index_sequence<INDEX...>>
  {
    using type = Concat<
      typename BaseItems<::reflect::base_at<INDEX, BASES>>::type...>;
  };

  // No scheme, no members; parse.hpp is where that becomes a refusal.
  template <typename Owner, bool>
  struct Flattened
  {
    using type = std::tuple<>;
  };

  template <typename Owner>
  struct Flattened<Owner, true>
  {
    using Bases = decltype(BASES_OF<Owner>);

    using type = Concat<
      typename BaseListItems<Bases,
        std::make_index_sequence<::reflect::bases_size(Bases{ })>>::type,
      typename OwnItems<Owner,
        std::make_index_sequence<
          ::reflect::scheme_size(SCHEME_OF<Owner>)>>::type>;
  };

  template <typename Owner>
  using ItemsOf = typename Flattened<Owner>::type;

  template <typename Owner>
  inline constexpr std::size_t ItemCountOf{
    std::tuple_size_v<ItemsOf<Owner>> };

  template <std::size_t INDEX, typename Owner>
  using ItemAt = std::tuple_element_t<INDEX, ItemsOf<Owner>>;

  template <typename Item>
  consteval auto ValidShortTags() -> bool
  {
    for (auto const tag : Item::tags::VALUES)
      if (tag.starts_with("-") && tag.size() != 2u) return false;
    return true;
  }

  template <typename Left, typename Right>
  consteval auto ShareShortTag() -> bool
  {
    for (auto const left : Left::tags::VALUES)
      if (left.starts_with("-"))
        for (auto const right : Right::tags::VALUES)
          if (left == right) return true;
    return false;
  }

  template <typename Left, typename Right>
  consteval auto ValidateShortPair() -> void
  {
    static_assert(!ShareShortTag<Left, Right>(),
      "two members claim the same short spelling. Both members are named "
      "in the template arguments above. Give them distinct short tags.");
  }

  template <typename Item>
  consteval auto ValidateMemberShortTags() -> void
  {
    static_assert(ValidShortTags<Item>(),
      "a short option tag must be a dash and exactly one character. "
      "The member is named in the template arguments above. Tags without "
      "a leading dash belong to other consumers and are left untouched.");
  }

  template <typename Owner, std::size_t INDEX>
  consteval auto ValidateShortTags() -> void
  {
    using Item = ItemAt<INDEX, Owner>;
    ValidateMemberShortTags<Item>();
    [&]<std::size_t ... PREVIOUS>(std::index_sequence<PREVIOUS...>) {
      (ValidateShortPair<ItemAt<PREVIOUS, Owner>, Item>(), ...);
    }(std::make_index_sequence<INDEX>{ });
  }

  // A lambda inside the fold expression ICEs gcc 16 (cp/pt.cc:17282).
  template <typename Owner, typename Step>
  constexpr auto ForEachItem(Step&& step) -> void
  {
    [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
      ((ValidateShortTags<Owner, INDEX>(),
        step.template operator()<INDEX>()), ...);
    }(std::make_index_sequence<ItemCountOf<Owner>>{ });
  }
}

namespace oxbox::cli
{
  using detail::scheme::HasMemberList;
  using detail::scheme::ItemAt;
  using detail::scheme::ItemCountOf;
  using detail::scheme::ItemsOf;
}
