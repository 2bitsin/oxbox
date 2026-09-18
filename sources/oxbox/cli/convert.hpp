#pragma once

// Command-line text to a C++ value, for an option's member and a
// positional's parameter alike. An enum converts through its reflected
// scheme, under the same external-name rule as options.

#include "oxbox/cli/errors.hpp"
#include "oxbox/cli/naming.hpp"

#include <_buildutil/reflect.hpp>

#include <charconv>
#include <concepts>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace oxbox::cli::detail::convert
{
  using utilities::TypeMismatch;

  template <typename Type>
  concept OptionalLike = requires(Type& slot) { slot = std::nullopt; };

  template <typename Enum> auto EnumValues() -> std::string;

  template <typename Enum>
  auto EnumFromText(std::string_view text, std::string_view option) -> Enum
  {
    constexpr auto SCHEME{ ::reflect::scheme_of<Enum>() };
    std::optional<Enum> found{ };

    // A lambda inside the fold expression ICEs gcc 16 (cp/pt.cc:17282).
    auto consider = [&]<std::size_t INDEX>() {
      using Item = decltype(::reflect::scheme_item<INDEX>(SCHEME));
      if (!found.has_value()
          && naming::Matches(naming::ExternalName<Item>(), text))
        found = Item::VALUE;
    };
    [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
      (consider.template operator()<INDEX>(), ...);
    }(std::make_index_sequence<::reflect::scheme_size(SCHEME)>{ });

    if (!found.has_value())
      throw TypeMismatch{ std::string{ option },
        std::format("{}, got '{}'", EnumValues<Enum>(), text) };
    return *found;
  }

  template <typename Enum>
  auto EnumValues() -> std::string
  {
    constexpr auto SCHEME{ ::reflect::scheme_of<Enum>() };
    std::string out{ "one of: " };
    auto append = [&]<std::size_t INDEX>() {
      using Item = decltype(::reflect::scheme_item<INDEX>(SCHEME));
      if constexpr (INDEX) out += ", ";
      out += naming::Spell(naming::ExternalName<Item>());
    };
    [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
      (append.template operator()<INDEX>(), ...);
    }(std::make_index_sequence<::reflect::scheme_size(SCHEME)>{ });
    return out;
  }

  // The label when there is one -- the lookup above accepts nothing else.
  template <typename Enum>
  auto EnumNameOf(Enum value) -> std::string_view
  {
    constexpr auto SCHEME{ ::reflect::scheme_of<Enum>() };
    std::string_view found{ };
    auto consider = [&]<std::size_t INDEX>() {
      using Item = decltype(::reflect::scheme_item<INDEX>(SCHEME));
      if (found.empty() && Item::VALUE == value)
        found = naming::ExternalName<Item>();
    };
    [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
      (consider.template operator()<INDEX>(), ...);
    }(std::make_index_sequence<::reflect::scheme_size(SCHEME)>{ });
    return found;
  }

  template <typename Type>
  auto FromText(std::string_view text, std::string_view option) -> Type
  {
    if constexpr (OptionalLike<Type>) {
      // Given at all means engaged, even when the text is empty.
      return Type{ FromText<typename Type::value_type>(text, option) };

    } else if constexpr (std::same_as<Type, bool>) {
      if (text == "true"  || text == "1" || text.empty()) return true;
      if (text == "false" || text == "0") return false;
      throw TypeMismatch{ std::string{ option },
        std::format("true or false, got '{}'", text) };

    } else if constexpr (std::same_as<Type, std::string>
                      || std::same_as<Type, std::string_view>) {
      return Type{ text };

    } else if constexpr (std::is_enum_v<Type>) {
      return EnumFromText<Type>(text, option);

    } else if constexpr (std::integral<Type> || std::floating_point<Type>) {
      Type value{ };
      auto const* const first{ text.data() };
      auto const* const last { text.data() + text.size() };
      auto const [stop, error]{ std::from_chars(first, last, value) };
      if (error != std::errc{ } || stop != last)
        throw TypeMismatch{ std::string{ option },
          std::format("{}, got '{}'",
            std::floating_point<Type> ? "a number" : "an integer", text) };
      return value;

    } else {
      static_assert(false,
        "this option's member type -- or this positional's parameter type "
        "-- cannot be built from command-line text. The type is named in "
        "the instantiation trace above. Supported: bool, the integral and "
        "floating-point types, std::string, std::string_view, a reflected "
        "enum, and std::optional of any of those. Declare it as one of "
        "those and convert it yourself, or give the type a conversion.");
    }
  }
}

namespace oxbox::cli
{
  using detail::convert::EnumNameOf;
  using detail::convert::EnumValues;
  using detail::convert::FromText;
}
