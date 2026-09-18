#pragma once
// What a function needs and what it guarantees, as code that runs in every
// build type. The consumer's buildutil option chooses the mode: stop where a
// contract breaks, complain and carry on, or ignore it.

#include "oxbox/platform/contract-report.hpp"

#include <cstdlib>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

namespace oxbox::platform::detail::contract
{
  using namespace contract_report;

  enum class ContractMode { IGNORE, COMPLAIN, STOP };

  inline constexpr std::string_view PRECONDITION{ "precondition" };
  inline constexpr std::string_view POSTCONDITION{ "postcondition" };
  inline constexpr std::string_view UNREACHABLE{ "unreachable" };
  inline constexpr std::string_view NOT_IMPLEMENTED{ "not implemented" };

  inline auto Report(std::string_view kind, std::string_view text,
                     std::source_location where) -> void
  {
    ReportContractFailure(std::format("{}: {}:{} {}: {}", kind,
      where.file_name(), where.line(), where.function_name(), text));
  }

  [[noreturn]] inline auto Fail(std::string_view kind, std::string_view text,
                                std::source_location where) -> void
  {
    Report(kind, text, where);
    std::abort();
  }

  // A broken contract in a mode that watches: STOP ends the program where it
  // broke, COMPLAIN says the same line and returns.
  template <ContractMode MODE>
  auto Break(std::string_view kind, std::string_view text,
             std::source_location where) -> void
  {
    if constexpr (MODE == ContractMode::STOP) Fail(kind, text, where);
    else                                      Report(kind, text, where);
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
      if constexpr (MODE == ContractMode::IGNORE) std::unreachable();
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
  using detail::contract::ContractMode;
  using detail::contract::Contracts;
}
