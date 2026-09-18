#pragma once

// .inc, not .hpp: buildutil's reflect generator claims any header that
// spells `reflect_scheme` and emits its own, which would collide with the
// hand-written schemes below -- and those are the point.

#include "oxbox/cli/main.hpp"

#include <array>
#include <optional>
#include <ranges>
#include <string_view>
#include <string>
#include <vector>

namespace oxbox::cli::unit_test
{
  // What each fixture saw, so "was it engaged, and with what?" is an
  // assertion. A private member is not an option, which is what makes
  // this channel free.
  struct Seen
  {
    std::string                       query { };
    std::optional<int>                limit { };
    std::optional<std::string>        extra { };
    std::optional<std::array<int, 2>> pair  { };
    std::array<std::optional<int>, 3> slots { };
    std::vector<std::string>          owned { };
    std::vector<std::string>          rest  { };
  };

  // The canonical signature: one required, one optional, nothing after.
  struct Find : Command
  {
    friend constexpr auto reflect_scheme(Find*);
    friend constexpr auto reflect_call_scheme(Find*);

    explicit Find(Seen* seen = nullptr) : _seen{ seen } { }

    auto operator() (std::string        query /* what to look for */,
                     std::optional<int> limit /* how many to show */
                    ) const -> CliResult
    {
      _seen->query = query;
      _seen->limit = limit;
      return{ };
    }

  private:
    Seen* _seen;
  };

  // A string of optionals at the end, filled one by one.
  struct Several : Command
  {
    friend constexpr auto reflect_scheme(Several*);
    friend constexpr auto reflect_call_scheme(Several*);

    explicit Several(Seen* seen = nullptr) : _seen{ seen } { }

    auto operator() (std::string                query /* what to look for */,
                     std::optional<int>         limit /* how many to show */,
                     std::optional<std::string> extra /* and one more */
                    ) const -> CliResult
    {
      _seen->query = query;
      _seen->limit = limit;
      _seen->extra = extra;
      return{ };
    }

  private:
    Seen* _seen;
  };

  // An optional and then a tail: the optional is greedy.
  struct Greedy : Command
  {
    friend constexpr auto reflect_scheme(Greedy*);
    friend constexpr auto reflect_call_scheme(Greedy*);

    explicit Greedy(Seen* seen = nullptr) : _seen{ seen } { }

    auto operator() (std::string            query /* what to look for */,
                     std::optional<int>     limit /* how many to show */,
                     RangeView<std::string> rest  /* whatever is left */
                    ) const -> CliResult
    {
      _seen->query = query;
      _seen->limit = limit;
      _seen->rest  = std::ranges::to<std::vector<std::string>>(rest);
      return{ };
    }

  private:
    Seen* _seen;
  };

  // A prefix-filled array of optionals: 0 to N, one by one.
  struct Slots : Command
  {
    friend constexpr auto reflect_scheme(Slots*);
    friend constexpr auto reflect_call_scheme(Slots*);

    explicit Slots(Seen* seen = nullptr) : _seen{ seen } { }

    auto operator() (std::string                       query /* what to look for */,
                     std::array<std::optional<int>, 3> limit /* up to three */
                    ) const -> CliResult
    {
      _seen->query = query;
      _seen->slots = limit;
      return{ };
    }

  private:
    Seen* _seen;
  };

  // An owning variadic positional, as distinct from the tail view: same
  // intake, and it outlives the vector the tokens came from.
  struct Owning : Command
  {
    friend constexpr auto reflect_scheme(Owning*);
    friend constexpr auto reflect_call_scheme(Owning*);

    explicit Owning(Seen* seen = nullptr) : _seen{ seen } { }

    auto operator() (std::string              query /* what to look for */,
                     std::vector<std::string> limit /* everything left */
                    ) const -> CliResult
    {
      _seen->query = query;
      _seen->owned = limit;
      return{ };
    }

  private:
    Seen* _seen;
  };

  // The same, holding views. Its elements point into the caller's own
  // storage, so it is what catches a container branch that materialised
  // a temporary to convert from.
  struct Viewing : Command
  {
    friend constexpr auto reflect_scheme(Viewing*);
    friend constexpr auto reflect_call_scheme(Viewing*);

    explicit Viewing(Seen* seen = nullptr) : _seen{ seen } { }

    auto operator() (std::string                   query /* what to look for */,
                     std::vector<std::string_view> limit /* everything left */
                    ) const -> CliResult
    {
      _seen->query = query;
      _seen->owned.clear();
      for (auto const one : limit) _seen->owned.emplace_back(one);
      return{ };
    }

  private:
    Seen* _seen;
  };

  // An optional of a shaped type: N tokens or none, never some.
  struct Shaped : Command
  {
    friend constexpr auto reflect_scheme(Shaped*);
    friend constexpr auto reflect_call_scheme(Shaped*);

    explicit Shaped(Seen* seen = nullptr) : _seen{ seen } { }

    auto operator() (std::string                       query /* what to look for */,
                     std::optional<std::array<int, 2>> pair  /* both or neither */
                    ) const -> CliResult
    {
      _seen->query = query;
      _seen->pair  = pair;
      return{ };
    }

  private:
    Seen* _seen;
  };
}

namespace oxbox::cli::unit_test
{
  constexpr auto reflect_scheme(Find*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_seen", &Find::_seen, "", true>>{ };
  }

  constexpr auto reflect_call_scheme(Find*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"query", "what to look for">,
      ::reflect::param_scheme<"limit", "how many to show">>{ };
  }

  constexpr auto reflect_scheme(Several*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_seen", &Several::_seen, "", true>>{ };
  }

  constexpr auto reflect_call_scheme(Several*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"query", "what to look for">,
      ::reflect::param_scheme<"limit", "how many to show">,
      ::reflect::param_scheme<"extra", "and one more">>{ };
  }

  constexpr auto reflect_scheme(Greedy*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_seen", &Greedy::_seen, "", true>>{ };
  }

  constexpr auto reflect_call_scheme(Greedy*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"query", "what to look for">,
      ::reflect::param_scheme<"limit", "how many to show">,
      ::reflect::param_scheme<"rest",  "whatever is left">>{ };
  }

  constexpr auto reflect_scheme(Slots*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_seen", &Slots::_seen, "", true>>{ };
  }

  constexpr auto reflect_call_scheme(Slots*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"query", "what to look for">,
      ::reflect::param_scheme<"limit", "up to three">>{ };
  }

  constexpr auto reflect_scheme(Owning*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_seen", &Owning::_seen, "", true>>{ };
  }

  constexpr auto reflect_call_scheme(Owning*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"query", "what to look for">,
      ::reflect::param_scheme<"limit", "everything left">>{ };
  }

  constexpr auto reflect_scheme(Viewing*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_seen", &Viewing::_seen, "", true>>{ };
  }

  constexpr auto reflect_call_scheme(Viewing*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"query", "what to look for">,
      ::reflect::param_scheme<"limit", "everything left">>{ };
  }

  constexpr auto reflect_scheme(Shaped*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"_seen", &Shaped::_seen, "", true>>{ };
  }

  constexpr auto reflect_call_scheme(Shaped*)
  {
    return ::reflect::call_scheme<
      ::reflect::param_scheme<"query", "what to look for">,
      ::reflect::param_scheme<"pair",  "both or neither">>{ };
  }
}
