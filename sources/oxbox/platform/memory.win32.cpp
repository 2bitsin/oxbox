#include "oxbox/platform/memory.hpp"

#include <format>
#include <stdexcept>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace oxbox::platform::detail::memory
{
  auto PageSize() noexcept -> std::size_t
  {
    static std::size_t const PROBED{ [] {
      ::SYSTEM_INFO info{ };
      ::GetSystemInfo(&info);
      return static_cast<std::size_t>(info.dwPageSize);
    }() };
    return PROBED;
  }

  auto AllocatePages(std::size_t bytes) -> WritableBytes
  {
    auto const mapped{ ::VirtualAlloc(nullptr, bytes,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) };
    if (mapped == nullptr)
    {
      // the last error first: the formatting below allocates, and allocating
      // is allowed to make Win32 calls that overwrite it
      auto const code{ static_cast<int>(::GetLastError()) };
      throw std::runtime_error{ std::format(
        "platform memory: VirtualAlloc of {:#x} bytes failed: {}",
        bytes, std::system_category().message(code)) };
    }
    return { static_cast<std::byte*>(mapped), bytes };
  }

  auto ReleasePages(WritableBytes pages) noexcept -> void
  {
    if (!pages.empty())
      ::VirtualFree(pages.data(), 0u, MEM_RELEASE);
  }
}
