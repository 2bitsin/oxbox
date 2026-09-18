#pragma once

// A command is a plain struct deriving from Command whose public members
// are its options; reflection reads the members and the comments beside
// them. Command is a stateless tag, so a command is an ordinary value type.

#include <compare>
#include <cstdint>
#include <string_view>
#include <string>
#include <type_traits>
#include <utility>

namespace oxbox::cli::detail::command
{
  struct Command
  {
    constexpr Command() = default;
    Command(Command const&) = default;
    Command(Command&&) noexcept = default;
    Command& operator = (Command const&) = default;
    Command& operator = (Command&&) noexcept = default;
    ~Command() = default;

    // One instance per subcommand type per thread; it outlives the run.
    template <typename Subcommand>
    [[nodiscard]] static auto Get() -> Subcommand&
    {
      static_assert(std::is_base_of_v<Command, Subcommand>,
        "Command::Get is for subcommands: the type must derive from "
        "Command.");
      static_assert(std::is_default_constructible_v<Subcommand>,
        "a subcommand is created on demand, so it must be default "
        "constructible. Give it a default constructor -- a constructor "
        "argument has nowhere to come from when the framework is the one "
        "doing the constructing.");
      static thread_local Subcommand instance{ };
      return instance;
    }
  };

  template <typename Type>
  concept CommandDerived =
    std::is_base_of_v<Command, std::remove_cvref_t<Type>>;

  // The axis is whether the command ran, not whether it succeeded.
  enum class CliStatus : std::uint8_t
  {
    RAN,
    HELP_SHOWN,
    NO_ACTION,     // reached a destination that declares no operator()
    USAGE_ERROR,
  };

  // The conventional shell code for misuse; 1 means it ran and failed.
  inline constexpr int USAGE_EXIT_CODE{ 2 };

  class CliResult
  {
  public:
    constexpr CliResult() = default;

    constexpr CliResult(int code) noexcept          // NOLINT(*-explicit-*)
    : _code{ code }
    {
    }

    [[nodiscard]] static auto HelpShown(std::string screen) -> CliResult
    { return CliResult{ 0, CliStatus::HELP_SHOWN, std::move(screen) }; }

    // A screen for the reader, but code 2: a script must not be told yes.
    [[nodiscard]] static auto NoAction(std::string screen) -> CliResult
    { return CliResult{ USAGE_EXIT_CODE, CliStatus::NO_ACTION,
                        std::move(screen) }; }

    [[nodiscard]] static auto UsageError(std::string reason) -> CliResult
    { return CliResult{ USAGE_EXIT_CODE, CliStatus::USAGE_ERROR,
                        std::move(reason) }; }

    // The status stays RAN; passing 0 as the code contradicts itself.
    [[nodiscard]] static auto Failed(int code, std::string reason)
      -> CliResult
    { return CliResult{ code, CliStatus::RAN, std::move(reason) }; }

    [[nodiscard]] constexpr auto Code() const noexcept -> int
    { return _code; }

    [[nodiscard]] constexpr auto Status() const noexcept -> CliStatus
    { return _status; }

    [[nodiscard]] constexpr auto Ok() const noexcept -> bool
    { return _code == 0; }

    // A screen is for the reader: stdout, printed whole, never prefixed.
    [[nodiscard]] constexpr auto ShowsScreen() const noexcept -> bool
    { return _status == CliStatus::HELP_SHOWN
          || _status == CliStatus::NO_ACTION; }

    // The library never prints it; an embedding caller chooses where.
    [[nodiscard]] auto Message() const& noexcept -> std::string_view
    { return _message; }

    [[nodiscard]] auto Message() && -> std::string
    { return std::move(_message); }

    // An exit code, not a truth value: zero is success, hence no operator bool.
    constexpr operator int() const noexcept          // NOLINT(*-explicit-*)
    { return _code; }

    auto operator == (CliResult const&) const -> bool = default;

  private:
    CliResult(int code, CliStatus status, std::string message)
    : _code{ code }, _status{ status }, _message{ std::move(message) }
    {
    }

    int         _code   { 0 };
    CliStatus   _status { CliStatus::RAN };
    std::string _message{ };
  };

  // An inline subcommand's segment: its empty member list is the answer.
  struct InlineSegment : Command
  {
  };
}

namespace oxbox::cli
{
  using detail::command::CliResult;
  using detail::command::CliStatus;
  using detail::command::Command;
  using detail::command::CommandDerived;
  using detail::command::InlineSegment;
  using detail::command::USAGE_EXIT_CODE;
}
