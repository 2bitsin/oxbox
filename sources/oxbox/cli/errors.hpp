#pragma once

// Command-line failures, every one deriving from ParseError. The one
// exception is utilities::TypeMismatch, raised by convert.hpp, so anything
// mapping cli failures to exit codes catches both families.

#include "oxbox/utilities/errors.hpp"

#include <format>
#include <string>
#include <string_view>

namespace oxbox::cli::detail::errors
{
  using utilities::ParseError;

  struct UnknownOption : ParseError
  {
    UnknownOption(std::string_view given, std::string_view closest)
    : ParseError{ closest.empty()
        ? std::format("unknown option '{}{}'",
                      given.starts_with("-") ? "" : "--", given)
        : std::format("unknown option '--{}'; did you mean '--{}'?",
                      given, closest) }
    , option{ given }
    , suggestion{ closest }
    {
    }

    std::string option;
    std::string suggestion;
  };

  // Values, not occurrences; the capacity comes from the member's type.
  struct RepeatedOption : ParseError
  {
    RepeatedOption(std::string_view given, std::size_t accepts)
    : ParseError{ accepts == 1u
        ? std::format("option '--{}' given more than once", given)
        : std::format("option '--{}' holds at most {} values",
                      given, accepts) }
    , option{ given }
    , capacity{ accepts }
    {
    }

    std::string option;
    std::size_t capacity;
  };

  struct MissingValue : ParseError
  {
    explicit MissingValue(std::string_view given)
    : ParseError{ std::format("option '--{}' needs a value", given) }
    , option{ given }
    {
    }

    std::string option;
  };

  struct MissingArgument : ParseError
  {
    MissingArgument(std::string_view wanted, std::string_view about)
    : ParseError{ about.empty()
        ? std::format("missing argument '{}'", wanted)
        : std::format("missing argument '{}': {}", wanted, about) }
    , parameter{ wanted }
    , description{ about }
    {
    }

    std::string parameter;
    std::string description;
  };

  struct UnexpectedArgument : ParseError
  {
    explicit UnexpectedArgument(std::string_view given)
    : ParseError{ std::format("unexpected argument '{}'", given) }
    , argument{ given }
    {
    }

    std::string argument;
  };

  struct SubcommandTakesNoValue : ParseError
  {
    explicit SubcommandTakesNoValue(std::string_view given)
    : ParseError{ std::format(
        "subcommand '--{}' takes no value; everything after it is passed "
        "to it", given) }
    , option{ given }
    {
    }

    std::string option;
  };

  // The `--name:key=value,key=value` syntax, misused; `reason` says how.
  struct MalformedMapping : ParseError
  {
    MalformedMapping(std::string_view given, std::string_view why)
    : ParseError{ std::format("option '--{}' {}", given, why) }
    , option{ given }
    , reason{ why }
    {
    }

    std::string option;
    std::string reason;
  };
}

namespace oxbox::cli
{
  using detail::errors::MalformedMapping;
  using detail::errors::MissingArgument;
  using detail::errors::MissingValue;
  using detail::errors::RepeatedOption;
  using detail::errors::SubcommandTakesNoValue;
  using detail::errors::UnexpectedArgument;
  using detail::errors::UnknownOption;
}
