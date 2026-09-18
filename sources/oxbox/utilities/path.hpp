#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace oxbox::utilities::detail::path
{
  /* This is janky as hell, I hate it */


  inline auto _FromU8String(std::u8string_view input) noexcept -> std::string_view {
    return{ std::bit_cast<char const*>(input.data()), input.size() };
  }
  inline auto PathToString(std::filesystem::path const& input) -> std::string {
    return std::string{ _FromU8String(input.u8string()) };    
  }

  inline auto _ToU8String(std::string_view sv) noexcept -> std::u8string_view {
    return std::u8string_view{
      std::bit_cast<char8_t const*>(sv.data()), sv.size() };
  }
  inline auto PathFromString(std::string_view sv) -> std::filesystem::path {
    return std::filesystem::path{ std::u8string{ _ToU8String(sv) } };
  }
}

namespace oxbox::utilities
{
  using detail::path::PathFromString;
  using detail::path::PathToString;
}
