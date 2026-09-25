#pragma once
// What a function needs and what it guarantees, as code that runs in every
// build type. The consumer's buildutil option chooses the mode: stop where a
// contract breaks, throw, complain and carry on, or ignore it.

#include "oxbox/platform/contract-report.hpp"

#include <cstdlib>
#include <format>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

namespace oxbox::platform::detail::contract
{
  using namespace contract_report;

  enum class ContractMode { IGNORE, COMPLAIN, STOP, THROW };

  inline constexpr std::string_view PRECONDITION{ "precondition" };
  inline constexpr std::string_view POSTCONDITION{ "postcondition" };
  inline constexpr std::string_view UNREACHABLE{ "unreachable" };
  inline constexpr std::string_view NOT_IMPLEMENTED{ "not implemented" };

  inline auto FailureLine(std::string_view kind, std::string_view text,
                          std::source_location where) -> std::string
  {
    return std::format("{}: {}:{} {}: {}", kind, where.file_name(),
                       where.line(), where.function_name(), text);
  }

  inline auto Report(std::string_view kind, std::string_view text,
                     std::source_location where) -> void
  {
    ReportContractFailure(FailureLine(kind, text, where));
  }

  // The line a caller catches instead of reading off the console, whole: the
  // kind, the text and the place, with `what()` the line `Report` said.
  class ContractFailure : public std::runtime_error
  {
  public:
    ContractFailure(std::string_view kind, std::string_view text,
                    std::source_location where)
    : std::runtime_error{ FailureLine(kind, text, where) },
      _kind{ kind }, _text{ text }, _where{ where }
    { }

    [[nodiscard]] auto Kind() const noexcept -> std::string const&
    { return _kind; }

    [[nodiscard]] auto Text() const noexcept -> std::string const&
    { return _text; }

    [[nodiscard]] auto Where() const noexcept -> std::source_location const&
    { return _where; }

  private:
    std::string          _kind;
    std::string          _text;
    std::source_location _where;
  };

  [[noreturn]] inline auto Fail(std::string_view kind, std::string_view text,
                                std::source_location where) -> void
  {
    Report(kind, text, where);
    std::abort();
  }

  // Said first, so a console that outlives the program has the line even
  // where nothing catches, then thrown for whoever does.
  [[noreturn]] inline auto Throw(std::string_view kind, std::string_view text,
                                 std::source_location where) -> void
  {
    Report(kind, text, where);
    throw ContractFailure{ kind, text, where };
  }

  // A broken contract in a mode that watches: STOP ends the program where it
  // broke, THROW leaves it to a handler, COMPLAIN says the line and returns.
  template <ContractMode MODE>
  auto Break(std::string_view kind, std::string_view text,
             std::source_location where) -> void
  {
    if      constexpr (MODE == ContractMode::STOP)  Fail(kind, text, where);
    else if constexpr (MODE == ContractMode::THROW) Throw(kind, text, where);
    else                                            Report(kind, text, where);
  }

  // A value a path was never written for, said as far as the type allows.
  template <typename _Type>
  auto Describe(_Type const& value) -> std::string
  {
    if constexpr (std::formattable<_Type const&, char>)
      return std::format("{}", value);
    else
      return typeid(_Type).name();
  }

  template <ContractMode MODE>
  struct Contracts
  {
    static constexpr auto Expects(bool held, std::string_view text,
        std::source_location where = std::source_location::current())
      -> void
    {
      if constexpr (MODE != ContractMode::IGNORE)
        if (!held) Break<MODE>(PRECONDITION, text, where);
    }

    static constexpr auto Ensures(bool held, std::string_view text,
        std::source_location where = std::source_location::current())
      -> void
    {
      if constexpr (MODE != ContractMode::IGNORE)
        if (!held) Break<MODE>(POSTCONDITION, text, where);
    }

    // A closed switch's default has no continuation, so COMPLAIN stops too.
    template <typename _Type>
    [[noreturn]] static auto Unreachable(_Type const& value,
        std::source_location where = std::source_location::current())
      -> void
    {
      if      constexpr (MODE == ContractMode::IGNORE) std::unreachable();
      else if constexpr (MODE == ContractMode::THROW)
        Throw(UNREACHABLE, Describe(value), where);
      else Fail(UNREACHABLE, Describe(value), where);
    }

    static constexpr auto NotImplemented(std::string_view text,
        std::source_location where = std::source_location::current())
      -> void
    {
      if constexpr (MODE != ContractMode::IGNORE)
        Break<MODE>(NOT_IMPLEMENTED, text, where);
    }
  };
}

namespace oxbox::platform
{
  using detail::contract::ContractFailure;
  using detail::contract::ContractMode;
  using detail::contract::Contracts;
}
