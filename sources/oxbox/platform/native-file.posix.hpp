#pragma once
// The posix road's own vocabulary, included only by this module's *.posix.cpp.

#include "oxbox/platform/native-file.hpp"

#include <filesystem>
#include <string>
#include <string_view>

#include <sys/stat.h>

namespace oxbox::platform::detail::native_file
{
  inline constexpr ::mode_t NEW_FILE_MODE{ 0644 };   // the umask narrows it

  // errno is read on entry: a successful allocation may still change it.
  auto Failure(std::string_view what,
               std::filesystem::path const& path) -> std::string;
  auto Failure(std::string_view what, std::filesystem::path const& path,
               std::size_t bytes) -> std::string;
  auto Failure(std::string_view what) -> std::string;

  inline auto Descriptor(NativeFile file) noexcept -> int
  { return static_cast<int>(file); }
}
