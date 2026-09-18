// NOLINTBEGIN(misc-non-private-member-variables-in-classes): the serialization DSL is a
// value/descriptor surface by design (module-wide ruling)
#pragma once



#include <format>
#include <ranges>
#include <string>
#include <string_view>

#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/format-pack.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/serializable.hpp"

namespace oxbox::serialization::detail::formatter
{
// 
  struct ParsedSpec
  {
    std::string_view name; // empty == default
  };

  // Append `obj` serialised as `format_name` to `out`, then copy into `it`.
  template <typename _T, typename _It>
  auto WriteSerialized(_T const& obj,
                       std::string_view format_name,
                       _It it) -> _It
  {
    StringSink sink;
    auto picked = AllFormats::Visit(format_name, [&]<typename _F>(_F) {
      Serialize<_F>(obj, sink);
      return true;
    });
    if (!picked) {
      throw std::format_error{
        std::format("unknown serialization format '{}'", format_name)};
    }
    return std::ranges::copy(sink.Out(), std::move(it)).out;
  }
}

template <typename _T>
  requires (oxbox::serialization::HasScheme<_T>
        ||  oxbox::serialization::HasEnumMap<_T>
        ||  oxbox::serialization::HasEncodeDecode<_T>)
struct std::formatter<_T, char>
{
  oxbox::serialization::detail::formatter::ParsedSpec spec{};

  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ ctx.begin() };
    auto const end{ ctx.end() };
    auto const start{ it };
    while (it != end && *it != '}') ++it;
    spec.name = std::string_view{ start, it };
    return it;
  }

  template <typename _Ctx>
  auto format(_T const& obj, _Ctx& ctx) const
  {
    auto const name{ spec.name.empty()
                       ? oxbox::serialization::DEFAULT_FORMAT_NAME
                       : spec.name };
    return oxbox::serialization::detail::formatter::WriteSerialized(
             obj, name, ctx.out());
  }
};
// NOLINTEND(misc-non-private-member-variables-in-classes)
