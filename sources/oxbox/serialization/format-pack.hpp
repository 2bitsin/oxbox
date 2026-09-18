#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <variant>

#include "oxbox/serialization/format-traits.hpp"

namespace oxbox::serialization
{
// 

  template <typename... _Format>
  struct FormatPack
  {
    using Variant = std::variant<_Format...>;

    static constexpr auto FromName(std::string_view name)
      -> std::optional<Variant>
    {
      std::optional<Variant> result;
      static_cast<void>((... || (FormatTraits<_Format>::name == name
                 ? (result = Variant{ _Format{} }, true)
                 : false)));
      return result;
    }

    static constexpr auto FromExtension(std::string_view ext)
      -> std::optional<Variant>
    {
      std::optional<Variant> result;
      static_cast<void>((... || (std::ranges::contains(FormatTraits<_Format>::extensions, ext)
                 ? (result = Variant{ _Format{} }, true)
                 : false)));
      return result;
    }

    static constexpr auto NameOf(Variant const& v) -> std::string_view
    {
      return std::visit([]<typename F>(F const&) {
        return FormatTraits<F>::name;
      }, v);
    }

    static constexpr auto ExtensionOf(Variant const& v) -> std::string_view
    {
      return std::visit([]<typename F>(F const&) -> std::string_view {
        return FormatTraits<F>::extensions.front();
      }, v);
    }

    static auto Names() -> std::span<std::string_view const>
    {
      static constexpr std::array names{ FormatTraits<_Format>::name... };
      return names;
    }

// 
    template <typename _Fn>
    static auto Visit(std::string_view name, _Fn&& fn)
      -> std::optional<std::invoke_result_t<_Fn, std::variant_alternative_t<0, Variant>>>
    {
      if (auto v = FromName(name))
        return std::visit(std::forward<_Fn>(fn), *v);
      return std::nullopt;
    }
  };

  // The single declaration site. New format goes here.
  using AllFormats = FormatPack<JsonFormat, YamlFormat,
                                XmlFormat, XmlPrettyFormat, BinaryFormat,
                                PositionalFormat>;
  using AnyFormat  = AllFormats::Variant;

  inline constexpr std::string_view DEFAULT_FORMAT_NAME{ "xml-pretty" };

  // Top-level shortcuts.
  inline constexpr auto FormatFromString(std::string_view name)
    -> std::optional<AnyFormat>
  {
    return AllFormats::FromName(name);
  }

  inline constexpr auto FormatFromExtension(std::string_view ext)
    -> std::optional<AnyFormat>
  {
    return AllFormats::FromExtension(ext);
  }
}
