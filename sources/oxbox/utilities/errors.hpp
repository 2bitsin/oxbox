#pragma once

// The project's exception family. ParseError and TypeMismatch are user
// misuse, InvalidArgument is a malformed scheme; a caller that also parses
// untrusted data must derive a specific type rather than catch ParseError.

#include "oxbox/utilities/path.hpp"

#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>


// std::format has no formatter for a path, and every message below prints one.
template <>
struct std::formatter<std::filesystem::path>
  : std::formatter<std::string_view>
{
  auto format(std::filesystem::path const& path, auto& ctx) const noexcept {    
    using namespace ::oxbox::utilities;
    using SVFmt = std::formatter<std::string_view>;
    return SVFmt::format(PathToString(path), ctx);
  }
};

namespace oxbox::utilities::detail::errors
{
  struct ParseError : std::runtime_error
  {
    explicit ParseError(std::string const& what)
    : std::runtime_error{ what }
    {
    }
  };

  struct MissingField : std::runtime_error
  {
    explicit MissingField(std::filesystem::path const& field)
    : std::runtime_error{ std::format("missing field: {}", field) }
    {
    }
  };

  // Not a ParseError: this one type is thrown both for something a user typed
  // and for something that arrived off a wire, and cli::Main maps it by hand.
  struct TypeMismatch : std::runtime_error
  {
    TypeMismatch(std::filesystem::path const& where,
                 std::string const&          expected)
    : std::runtime_error{ std::format(
        "type mismatch at {}: expected {}", where, expected) }
    {
    }
  };

  struct FileOpenError : std::runtime_error
  {
    explicit FileOpenError(std::filesystem::path const& path)
    : std::runtime_error{ std::format("cannot open file: {}", path) }
    {
    }
  };

  struct InvalidArgument : std::runtime_error
  {
    InvalidArgument(std::string const& source,
                    std::string const& reason)
    : std::runtime_error{ std::format("{}: {}", source, reason) }
    {
    }
  };

  // A byte source or sink failed mid-transfer (docs/streaming-io.md §2).
  struct IoError : std::runtime_error
  {
    explicit IoError(std::string const& what)
    : std::runtime_error{ what }
    {
    }
  };
}

namespace oxbox::utilities
{
  using detail::errors::FileOpenError;
  using detail::errors::InvalidArgument;
  using detail::errors::IoError;
  using detail::errors::MissingField;
  using detail::errors::ParseError;
  using detail::errors::TypeMismatch;
}
