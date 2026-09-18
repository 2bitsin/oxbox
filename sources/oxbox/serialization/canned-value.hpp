#pragma once

#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/format-json.hpp"
#include "oxbox/serialization/format-yaml.hpp"
#include "oxbox/serialization/read-walker.hpp"
#include "oxbox/serialization/serializable.hpp"

namespace oxbox::serialization
{
  using Canned = std::variant<YAML::Node, nlohmann::json>;

  template <> struct IsCannedT<Canned> : std::true_type {};


  template <typename T>
  auto Uncan(Canned const& c) -> T
  {
    return std::visit(
      []<typename N>(N const& node) -> T {
        T out{};
        if constexpr (std::is_same_v<N, YAML::Node>) {
          typename YamlFormat::template Reader<detail::ProbeSource> r{node};
          detail::ReadWalker walker{r};
          walker(out);
        } else {
          typename JsonFormat::template Reader<detail::ProbeSource> r{node};
          detail::ReadWalker walker{r};
          walker(out);
        }
        if constexpr (HasRestoreHook<T>) RunRestoreHook(out);
        return out;
      },
      c);
  }
}
