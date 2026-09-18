#pragma once
// argv as a C API wants to see it, owned: the C API keeps the pointers and
// they point into this object. Slot zero is the program name; the trailing
// nullptr is part of the contract, and Argc() does not count it.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oxbox::cli::detail::owned_argv
{
  class OwnedArgv
  {
  public:
    OwnedArgv(std::string_view program, std::span<std::string const> arguments)
    {
      _storage.reserve(arguments.size() + 1u);
      _storage.emplace_back(program);
      _storage.append_range(arguments);

      // The reserve above keeps a reallocation from invalidating these.
      _pointers.reserve(_storage.size() + 1u);
      for (std::string& one : _storage)
        _pointers.push_back(one.data());
      _pointers.push_back(nullptr);
    }

    // A copy or a move keeps the pointers and loses what they point at.
    OwnedArgv(OwnedArgv const&)                      = delete;
    OwnedArgv(OwnedArgv&&)                           = delete;
    auto operator = (OwnedArgv const&) -> OwnedArgv& = delete;
    auto operator = (OwnedArgv&&)      -> OwnedArgv& = delete;
    ~OwnedArgv()                                     = default;

    [[nodiscard]] auto Argc() const noexcept -> int
    { return static_cast<int>(_pointers.size()) - 1; }

    [[nodiscard]] auto Argv() noexcept -> char** { return _pointers.data(); }

  private:
    std::vector<std::string> _storage;
    std::vector<char*>       _pointers;  // into _storage, plus the terminator
  };
}

namespace oxbox::cli
{
  using detail::owned_argv::OwnedArgv;
}
