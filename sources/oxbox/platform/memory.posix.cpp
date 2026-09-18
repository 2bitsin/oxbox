#include "oxbox/platform/memory.hpp"

#include <cerrno>
#include <format>
#include <stdexcept>
#include <system_error>

#include <sys/mman.h>
#include <unistd.h>

namespace oxbox::platform::detail::memory
{
  auto PageSize() noexcept -> std::size_t
  {
    static std::size_t const PROBED{ [] {
      auto const reported{ ::sysconf(_SC_PAGESIZE) };
      return reported > 0 ? static_cast<std::size_t>(reported) : PAGE_ALIGNMENT;
    }() };
    return PROBED;
  }

  auto AllocatePages(std::size_t bytes) -> WritableBytes
  {
    auto const mapped{ ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) };
    if (mapped == MAP_FAILED)
    {
      // errno first: the formatting below allocates, and a successful
      // allocation is not required to leave it alone
      auto const code{ errno };
      throw std::runtime_error{ std::format(
        "platform memory: mmap of {:#x} bytes failed: {}",
        bytes, std::system_category().message(code)) };
    }
    return { static_cast<std::byte*>(mapped), bytes };
  }

  auto ReleasePages(WritableBytes pages) noexcept -> void
  {
    if (!pages.empty())
      ::munmap(pages.data(), pages.size());
  }
}
