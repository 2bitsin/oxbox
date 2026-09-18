#pragma once

// The help screen, rendered from the scheme. It covers the whole line:
// the destination's usage, arguments, options and subcommands, then one
// options section per ancestor, nearest first.

#include "oxbox/cli/command.hpp"
#include "oxbox/cli/convert.hpp"
#include "oxbox/cli/invoke.hpp"
#include "oxbox/cli/member-role.hpp"
#include "oxbox/cli/name-lookup.hpp"
#include "oxbox/cli/naming.hpp"
#include "oxbox/cli/parse.hpp"
#include "oxbox/cli/rest-collector.hpp"
#include "oxbox/cli/scheme.hpp"

#include <_buildutil/reflect.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <format>
#include <ranges>
#include <string_view>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace oxbox::cli::detail::help
{
  namespace scheme = detail::scheme;

  inline constexpr std::size_t LINE_WIDTH { 78u };
  inline constexpr std::size_t NAME_COLUMN{ 2u  };
  inline constexpr std::size_t GAP        { 4u  };

  struct Row
  {
    std::string name;
    std::string comment;
    std::string detail;      // "(default: x)", and what an enum accepts
  };

  // The title is owned because an ancestor's is built from the trigger word.
  struct Section
  {
    std::string      title;
    std::vector<Row> rows;
  };

  // The root is the one layer no word reached, so it has no title to read.
  inline constexpr std::string_view ROOT_LABEL{ "root" };

  // A value as the user would have to type it back.
  template <typename Type>
  auto Show(Type const& value) -> std::string
  {
    if constexpr (std::same_as<Type, bool>)
      return value ? "true" : "false";
    else if constexpr (std::same_as<Type, std::string>
                    || std::same_as<Type, std::string_view>)
      return std::format("\"{}\"", value);
    else if constexpr (std::is_enum_v<Type>)
      return naming::Spell(convert::EnumNameOf(value));
    else if constexpr (requires { value.has_value(); })
      return value.has_value() ? Show(*value) : std::string{ "unset" };
    else if constexpr (requires { std::format("{}", value); })
      return std::format("{}", value);
    else
      return { };
  }

  template <typename Type>
  auto ValueName(std::string_view name) -> std::string
  {
    if constexpr (convert::OptionalLike<Type>)
      return ValueName<typename Type::value_type>(name);
    else if constexpr (std::same_as<Type, std::string>
                    || std::same_as<Type, std::string_view>)
      return "string";
    else if constexpr (std::same_as<Type, bool>) return "bool";
    else if constexpr (std::integral<Type>) return "int";
    else if constexpr (std::floating_point<Type>) return "number";
    else return std::string{ name };
  }

  // Single newlines join a paragraph; blank lines separate paragraphs.
  // Each paragraph folds to the available width.
  inline auto Wrap(std::string_view text, std::size_t width)
    -> std::vector<std::string>
  {
    std::vector<std::string> paragraphs;
    std::string joined;
    for (auto const source : std::views::split(text, '\n')) {
      std::string_view line{ source };
      auto const first{ line.find_first_not_of(" \t\r") };
      if (first == std::string_view::npos) {
        if (!joined.empty()) {
          paragraphs.push_back(std::move(joined));
          joined.clear();
        }
        continue;
      }
      line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1u);
      if (!joined.empty()) joined += ' ';
      joined += line;
    }
    if (!joined.empty()) paragraphs.push_back(std::move(joined));

    std::vector<std::string> lines;
    for (auto const& paragraph : paragraphs) {
      if (!lines.empty()) lines.emplace_back();
      std::string current;
      for (auto const word : std::views::split(paragraph, ' ')) {
        std::string_view const piece{ word };
        if (piece.empty()) continue;
        if (!current.empty() && current.size() + 1u + piece.size() > width) {
          lines.push_back(current);
          current.clear();
        }
        if (!current.empty()) current += ' ';
        current += piece;
      }
      lines.push_back(current);
    }
    return lines;
  }

  // Padding, not a format string: a dynamic `{:{}}` width must be one of
  // the integer types libstdc++ accepts, which std::size_t is not everywhere.
  inline auto Blanks(std::size_t count) -> std::string
  {
    return std::string(count, ' ');
  }

  // A row with no right column is just its name: no invisible trailing blanks.
  inline auto Render(Row const& row, std::size_t widest, std::size_t width)
    -> std::string
  {
    auto const left{ Blanks(NAME_COLUMN) + row.name };
    auto text{ Wrap(row.comment, width) };
    if (!row.detail.empty()) text.push_back(row.detail);
    if (text.empty() || (text.size() == 1u && text.front().empty()))
      return left + '\n';

    auto const indent{ NAME_COLUMN + widest + GAP };
    std::string out;
    for (std::size_t line{ 0u }; line < text.size(); ++line) {
      out += line == 0u
        ? left + Blanks(widest - row.name.size() + GAP)
        : Blanks(indent);
      out += text[line];
      out += '\n';
    }
    return out;
  }

  // A command that is not default-constructible has no defaults to read.
  struct NoDefaults
  {
  };

  // `destination` is whether the line ends here: the --help and collector
  // rows belong to the end of the line, not to a layer passed through.
  template <CommandDerived CliApp>
  auto OptionRows(bool destination = true) -> std::vector<Row>
  {
    constexpr bool KNOWN{ std::default_initializable<CliApp> };

    auto const defaults{ [] {
      if constexpr (KNOWN) return CliApp{ };
      else                 return NoDefaults{ };
    }() };
    std::vector<Row> rows;

    auto add = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, CliApp>;
      if constexpr (member_role::IsOption<Item, CliApp>()) {
        // no comment, no listing: that is how a member opts out
        if (Item::COMMENT.empty()) return;
        using Member = std::remove_cvref_t<
          decltype(::reflect::member_of<Item>(std::declval<CliApp&>()))>;
        std::string detail{ };
        if constexpr (KNOWN)
          detail = std::format(
            "(default: {})",
            Show<Member>(::reflect::member_of<Item>(defaults)));
        if constexpr (std::is_enum_v<Member>)
          detail = convert::EnumValues<Member>()
                 + (detail.empty() ? "" : "; ") + detail;
        auto name{ naming::Spell(naming::ExternalName<Item>()) };
        std::string label;
        for (auto const tag : Item::tags::VALUES)
          if (tag.starts_with("-")) label += std::string{ tag } + ", ";
        label += "--" + name;
        if constexpr (!std::same_as<Member, bool>)
          label += " <" + ValueName<Member>(name) + ">";
        rows.push_back({ std::move(label), std::string{ Item::COMMENT },
                         std::move(detail) });

      } else if constexpr (member_role::IsRestCollector<Item, CliApp>()) {
        if (!destination) return;
        // The member's own name and not the label: here the label is the
        // marker, and `-- <-->...` would name nothing.
        rows.push_back({ "-- <" + naming::Spell(Item::NAME_STRING) + ">...",
                         std::string{ Item::COMMENT }, { } });
      }
    };
    scheme::ForEachItem<CliApp>(add);

    // A command that declares `help` itself owns the name.
    if (destination && !name_lookup::DeclaresHelp<CliApp>())
      rows.push_back({ name_lookup::ClaimsShort<CliApp>("-h")
                         ? "--help" : "-h, --help",
                       "show this screen", { } });
    return rows;
  }

  // Asked of a signature, because the line does not always end at a command.
  template <typename Sig>
  auto ArgumentRows() -> std::vector<Row>
  {
    using Call = typename Sig::Call;

    constexpr Call CALL{ };

    std::vector<Row> rows;
    auto add = [&]<std::size_t INDEX>() {
      using Item = decltype(::reflect::scheme_item<INDEX>(CALL));
      // Listed whether or not it says anything: the slot is there either way.
      rows.push_back({ naming::Spell(naming::ExternalName<Item>()),
                       std::string{ Item::COMMENT }, { } });
    };
    [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
      (add.template operator()<INDEX>(), ...);
    }(std::make_index_sequence<::reflect::scheme_size(CALL)>{ });
    return rows;
  }

  // Each shown the way it is triggered; the two method kinds look the same.
  template <CommandDerived CliApp>
  auto SubcommandRows() -> std::vector<Row>
  {
    std::vector<Row> rows;

    auto add_member = [&]<std::size_t INDEX>() {
      using Item = scheme::ItemAt<INDEX, CliApp>;
      if constexpr (member_role::IsSubcommand<Item, CliApp>())
        rows.push_back({ "--" + naming::Spell(naming::ExternalName<Item>()),
                         std::string{ Item::COMMENT }, { } });
    };
    scheme::ForEachItem<CliApp>(add_member);

    if constexpr (::reflect::interface_reflected<CliApp>) {
      constexpr auto INTERFACE{ ::reflect::interface_scheme_of<CliApp>() };
      auto add_method = [&]<std::size_t INDEX>() {
        using Item = decltype(::reflect::scheme_item<INDEX>(INTERFACE));
        if constexpr (member_role::IsSubcommandMethod<Item>())
          rows.push_back({ naming::Spell(naming::ExternalName<Item>()),
                           std::string{ Item::COMMENT }, { } });
      };
      [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
        (add_method.template operator()<INDEX>(), ...);
      }(std::make_index_sequence<::reflect::scheme_size(INTERFACE)>{ });
    }
    return rows;
  }

  // `trail` is the trigger words in order, each followed by its own
  // `[options]` slot -- where the line accepts that layer's options.
  template <typename Sig>
  auto UsageLine(std::string_view program,
                 std::vector<std::string_view> const& trail = { },
                 bool collects = false)
    -> std::string
  {
    using Parameters = typename Sig::Parameters;
    using Call       = typename Sig::Call;

    constexpr Call CALL { };
    constexpr auto COUNT{ std::tuple_size_v<Parameters> };

    static_assert(::reflect::scheme_size(CALL) == COUNT,
      "the reflected call scheme and the signature it names disagree about "
      "how many parameters there are: this is a stale reflect header, and "
      "invoke.hpp says the same thing at greater length.");

    std::string out{ std::format("usage: {} [options]", program) };
    for (auto const word : trail)
      out += std::format(" {} [options]", word);

    auto add = [&]<std::size_t INDEX>() {
      using Param = std::remove_cvref_t<
        std::tuple_element_t<INDEX, Parameters>>;
      // Bounds-checked in invoke.hpp, so a drifted scheme is said once.
      auto const spelled{ naming::Spell(invoke::ParamNameOf<INDEX, Sig>()) };
      // Read off the intake range, so the screen cannot disagree with what
      // binding will do.
      if constexpr (invoke::MaxIntake<Param>() == invoke::UNBOUNDED_INTAKE)
        out += std::format(" [{}...]", spelled);
      else if constexpr (invoke::MinIntake<Param>() == 0u)
        out += std::format(" [{}]", spelled);
      else
        out += std::format(" <{}>", spelled);
    };
    [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
      (add.template operator()<INDEX>(), ...);
    }(std::make_index_sequence<COUNT>{ });

    // Last, because everything after the sentinel is the collector's.
    if (collects) out += " [-- ...]";

    out += '\n';
    return out;
  }

  // Layer zero is the root; every other layer answers to trail[depth - 1].
  inline auto LayerLabel(std::size_t depth,
                         std::vector<std::string_view> const& trail,
                         std::string_view program) -> std::string
  {
    if (depth == 0u)
      return std::string{ program.empty() ? ROOT_LABEL : program };
    return depth - 1u < trail.size()
      ? std::string{ trail[depth - 1u] }
      : std::string{ };
  }

  // `Ancestors` is the chain above the destination, root first, and `trail`
  // the trigger words in that order, one per descent. `Sig` is separate
  // from `Destination`: an inline subcommand's arguments belong to a method.
  template <typename Sig, CommandDerived Destination,
            CommandDerived ... Ancestors>
  auto FormatScreen(std::vector<std::string_view> const& trail,
                    std::string_view program) -> std::string
  {
    constexpr auto DEPTH{ sizeof...(Ancestors) };

    std::vector<Section> sections{
      { "Arguments",   ArgumentRows  <Sig>()              },
      { "Options",     OptionRows    <Destination>(true)  },
      { "Subcommands", SubcommandRows<Destination>()      } };

    if constexpr (DEPTH > 0u) {
      using Chain = std::tuple<Ancestors...>;
      std::vector<Section> above(DEPTH);
      auto add = [&]<std::size_t INDEX>() {
        using Layer = std::tuple_element_t<INDEX, Chain>;
        // The pack is root first, so it is laid down back to front.
        above[DEPTH - 1u - INDEX] = Section{
          LayerLabel(INDEX, trail, program) + " options",
          OptionRows<Layer>(false) };
      };
      [&]<std::size_t ... INDEX>(std::index_sequence<INDEX...>) {
        (add.template operator()<INDEX>(), ...);
      }(std::make_index_sequence<DEPTH>{ });

      for (auto& section : above) sections.push_back(std::move(section));
    }

    // One measurement over every section: the screen is one table.
    std::size_t widest{ 0u };
    for (auto const& section : sections)
      for (auto const& row : section.rows)
        widest = std::max(widest, row.name.size());

    auto const indent{ NAME_COLUMN + widest + GAP };
    auto const width { indent < LINE_WIDTH ? LINE_WIDTH - indent : 40u };

    std::string out;
    if (!program.empty())
      out += UsageLine<Sig>(program, trail,
                            rest_collector::HAS_REST_COLLECTOR<Destination>);

    for (auto const& section : sections) {
      if (section.rows.empty()) continue;
      if (!out.empty()) out += '\n';
      out += section.title;
      out += ":\n";
      for (auto const& row : section.rows) out += Render(row, widest, width);
    }
    return out;
  }

  template <CommandDerived Destination, CommandDerived ... Ancestors>
  auto FormatChainHelp(std::vector<std::string_view> const& trail = { },
                       std::string_view program = { }) -> std::string
  {
    return FormatScreen<invoke::SignatureOf<Destination>,
                        Destination, Ancestors...>(trail, program);
  }

  // The owner becomes the nearest ancestor, one longer than the chain looks.
  template <typename Item, CommandDerived Owner, CommandDerived ... Ancestors>
  auto FormatInlineHelp(std::vector<std::string_view> const& trail = { },
                        std::string_view program = { }) -> std::string
  {
    return FormatScreen<invoke::MethodSignature<Item>,
                        command::InlineSegment, Ancestors..., Owner>(
      trail, program);
  }

  template <CommandDerived CliApp>
  auto FormatHelp(std::string_view program = { }) -> std::string
  {
    return FormatChainHelp<CliApp>({ }, program);
  }
}

namespace oxbox::cli
{
  using detail::help::FormatChainHelp;
  using detail::help::FormatHelp;
  using detail::help::FormatInlineHelp;
}
