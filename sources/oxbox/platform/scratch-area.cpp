// A directory created and removed is std::filesystem's job on every host, so
// this road has no per-host twin.

#include "oxbox/platform/scratch-area.hpp"

#include "oxbox/utilities/path.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <random>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace oxbox::platform::detail::scratch_area
{
  namespace
  {
    // The token says which process; without a per-area serial two areas asked
    // for one purpose in one process would name one directory.
    auto NextSerial() noexcept -> std::uint32_t
    {
      static std::atomic<std::uint32_t> next{ 0u };
      return next.fetch_add(1u, std::memory_order_relaxed);
    }
  }

  // random_device xor the clock: the standard does not promise random_device
  // is non-deterministic, and two processes started in the same second would
  // then collide.
  auto ScratchToken() noexcept -> std::uint64_t
  {
    static std::uint64_t const TOKEN{ []
    {
      std::random_device entropy;
      auto const drawn{ (static_cast<std::uint64_t>(entropy()) << 32u) | entropy() };
      auto const now{ static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()) };
      return drawn ^ now;
    }() };
    return TOKEN;
  }

  auto ScratchDirectory(std::string_view purpose, std::string_view program)
    -> std::filesystem::path
  {
    std::error_code failure;
    auto const temp{ std::filesystem::temp_directory_path(failure) };
    if (failure)
      throw std::runtime_error{ std::format(
        "platform: no temp directory to make a scratch area in: {}",
        failure.message()) };

    auto const path{ temp / std::format("{}-{:016x}-{}-{}", program,
                                        ScratchToken(), NextSerial(), purpose) };

    // The parent is the system temp directory and this one has to be new, so
    // create_directory and not create_directories.
    auto const made{ std::filesystem::create_directory(path, failure) };
    if (failure)
      throw std::runtime_error{ std::format(
        "platform: cannot create scratch area '{}': {}",
        utilities::PathToString(path), failure.message()) };
    if (!made)
      throw std::runtime_error{ std::format(
        "platform: scratch area '{}' already exists",
        utilities::PathToString(path)) };
    return path;
  }

  auto ScratchArea::Remove() noexcept -> void
  {
    if (_path.empty())
      return;
    std::error_code ignored;
    std::filesystem::remove_all(_path, ignored);
  }
}
