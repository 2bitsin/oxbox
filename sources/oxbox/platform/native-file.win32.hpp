#pragma once

#include "oxbox/platform/native-file.hpp"

#include <bit>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <winioctl.h>   // FSCTL_SET_SPARSE, which windows.h alone does not carry

namespace oxbox::platform::detail::native_file
{
  // WriteFile counts in a DWORD, so a bigger buffer goes out in pieces.
  inline constexpr std::size_t MAX_WRITE_CHUNK{ 0x40000000u };

  // DIRECTORY, SPARSE_FILE and ENCRYPTED belong to the file rather than its
  // replacement; READONLY is carried and refuses the next replacing rename.
  inline constexpr ::DWORD CARRIED_ATTRIBUTES{
    FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN
  | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_OFFLINE
  | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM
  | FILE_ATTRIBUTE_TEMPORARY };

  // GetLastError() is read on entry: allocating can make win32 calls of its own.
  auto Failure(std::string_view what,
               std::filesystem::path const& path) -> std::string;
  auto Failure(std::string_view what, std::filesystem::path const& path,
               std::size_t bytes) -> std::string;
  auto Failure(std::string_view what) -> std::string;

  inline auto Handle(NativeFile file) noexcept -> ::HANDLE
  { return std::bit_cast<::HANDLE>(file); }

  // CreateFile fails with INVALID_HANDLE_VALUE and CreateFileMapping with null.
  inline auto AsNative(::HANDLE handle) noexcept -> NativeFile
  { return handle == nullptr || handle == INVALID_HANDLE_VALUE
         ? NO_FILE : std::bit_cast<NativeFile>(handle); }
}
