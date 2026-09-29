#pragma once
// What a function needs and what it guarantees, as code that runs in every
// build type. The consumer's buildutil option chooses the mode: stop where a
// contract breaks, throw, complain and carry on, or ignore it.

#include "oxbox/platform/contract-report.hpp"

#include <cstdlib>
#include <format>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

namespace oxbox::platform::detail::contract
{
  using namespace contract_report;

  enum class ContractMode { OFF, COMPLAIN, STOP, THROW };

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
    static constexpr auto Expects(bool held,
        std::string_view     text,
        std::source_location where = std::source_location::current())
      -> void
    {
      if constexpr (MODE != ContractMode::OFF)
        if (!held) Break<MODE>(PRECONDITION, text, where);
    }

    // An optional is held when it has a value.
    template <typename _Type>
    static constexpr auto Expects(std::optional<_Type> const& held,
        std::string_view     text,
        std::source_location where = std::source_location::current())
      -> void
    {
      Expects(held.has_value(), text, where);
    }

    static constexpr auto Ensures(bool held,
        std::string_view     text,
        std::source_location where = std::source_location::current())
      -> void
    {
      if constexpr (MODE != ContractMode::OFF)
        if (!held) Break<MODE>(POSTCONDITION, text, where);
    }

    template <typename _Type>
    static constexpr auto Ensures(std::optional<_Type> const& held,
        std::string_view     text,
        std::source_location where = std::source_location::current())
      -> void
    {
      Ensures(held.has_value(), text, where);
    }

    // A closed switch's default has no continuation, so COMPLAIN stops too.
    template <typename _Type>
    [[noreturn]] static auto Unreachable(_Type const& value,
        std::source_location where = std::source_location::current())
      -> void
    {
      if      constexpr (MODE == ContractMode::OFF) std::unreachable();
      else if constexpr (MODE == ContractMode::THROW)
        Throw(UNREACHABLE, Describe(value), where);
      else Fail(UNREACHABLE, Describe(value), where);
    }

    static constexpr auto NotImplemented(std::string_view text,
        std::source_location where = std::source_location::current())
      -> void
    {
      if constexpr (MODE != ContractMode::OFF)
        Break<MODE>(NOT_IMPLEMENTED, text, where);
    }
  };

  // Contracts<MODE> refused unless the preprocessor was told the same mode.
  template <ContractMode MODE, bool PREPROCESSOR_IGNORES>
  struct Agreed : Contracts<MODE>
  {
    static_assert((MODE == ContractMode::OFF) == PREPROCESSOR_IGNORES,
                  "OXBOX_CONTRACTS_IGNORE is defined exactly when "
                  "OXBOX_CONTRACT_MODE is ContractMode::OFF");

    static constexpr bool IGNORED{ MODE == ContractMode::OFF };
  };
}

// OXBOX_CONTRACT_MODE, a ContractMode constant expression, is read at each
// use, OXBOX_CONTRACTS_IGNORE at this include; the parentheses keep the
// template comma whole inside another macro's argument.
#ifdef OXBOX_CONTRACTS_IGNORE
  // NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the condition is left out
  #define OXBOX_EXPECTS(...) \
    static_cast<void>(::oxbox::platform::detail::contract:: \
                        Agreed<OXBOX_CONTRACT_MODE, true>::IGNORED)
  // NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the condition is left out
  #define OXBOX_ENSURES(...) \
    static_cast<void>(::oxbox::platform::detail::contract:: \
                        Agreed<OXBOX_CONTRACT_MODE, true>::IGNORED)
#else
  // NOLINTNEXTLINE(cppcoreguidelines-macro-usage): one spelling in every mode
  #define OXBOX_EXPECTS(...) \
    (::oxbox::platform::detail::contract:: \
       Agreed<OXBOX_CONTRACT_MODE, false>::Expects(__VA_ARGS__))
  // NOLINTNEXTLINE(cppcoreguidelines-macro-usage): as OXBOX_EXPECTS
  #define OXBOX_ENSURES(...) \
    (::oxbox::platform::detail::contract:: \
       Agreed<OXBOX_CONTRACT_MODE, false>::Ensures(__VA_ARGS__))
#endif

namespace oxbox::platform
{
  using detail::contract::ContractFailure;
  using detail::contract::ContractMode;
  using detail::contract::Contracts;
}
