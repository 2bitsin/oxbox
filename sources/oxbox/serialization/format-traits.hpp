#pragma once

#include <array>
#include <ios>
#include <string_view>

#include "oxbox/serialization/format-binary.hpp"
#include "oxbox/serialization/format-json.hpp"
#include "oxbox/serialization/format-positional.hpp"
#include "oxbox/serialization/format-xml.hpp"
#include "oxbox/serialization/format-yaml.hpp"

namespace oxbox::serialization
{
// 
  template <typename _Format>
  struct FormatTraits;

  template <>
  struct FormatTraits<JsonFormat>
  {
    static constexpr std::string_view  name      { "json" };
    static constexpr std::string_view  separator { "\n" };
    static constexpr std::ios::openmode openmode { };
// 
    static constexpr std::array<std::string_view, 1> extensions{ ".json" };
  };

  template <>
  struct FormatTraits<YamlFormat>
  {
    static constexpr std::string_view  name      { "yaml" };
    static constexpr std::string_view  separator { "---\n" };
    static constexpr std::ios::openmode openmode { };
    static constexpr std::array<std::string_view, 2> extensions{ ".yaml", ".yml" };
  };

  template <>
  struct FormatTraits<XmlFormat>
  {
    static constexpr std::string_view  name      { "xml" };
    static constexpr std::string_view  separator { "<!-- // -->\n" };
    static constexpr std::ios::openmode openmode { };
    static constexpr std::array<std::string_view, 1> extensions{ ".xml" };
  };

  template <>
  struct FormatTraits<XmlPrettyFormat>
  {
    static constexpr std::string_view  name      { "xml-pretty" };
    static constexpr std::string_view  separator { "<!-- // -->\n" };
    static constexpr std::ios::openmode openmode { };
    static constexpr std::array<std::string_view, 1> extensions{ ".xml" };
  };

  // Length-prefixed binary; opened binary so the bytes survive untranslated.
  template <>
  struct FormatTraits<BinaryFormat>
  {
    static constexpr std::string_view  name      { "binary" };
    static constexpr std::string_view  separator { "" };
    static constexpr std::ios::openmode openmode { std::ios::binary };
    static constexpr std::array<std::string_view, 1> extensions{ ".bsx" };
  };
  template <>
  struct FormatTraits<PositionalFormat>
  {
    static constexpr std::string_view name{ "positional" };
    static constexpr std::string_view separator{ "" };
    static constexpr std::ios::openmode openmode{ std::ios::binary };
    static constexpr std::array<std::string_view, 1> extensions{ ".bsp" };
  };
}
