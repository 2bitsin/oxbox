#pragma once
// What a resilient walk produced, shared by both transcode tiers.

#include "oxbox/utilities/transcode.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <vector>

namespace oxbox::utilities::test
{
  // A walk that spells the right codepoints but eats a tail it should have
  // carried is still wrong, so the leftover is part of the answer.
  struct Walked
  {
    std::vector<std::uint32_t> spelled;
    std::size_t                replacements{ 0u };
    std::size_t                left        { 0u };
  };
  inline auto AsValues(std::vector<char32_t> const& spelled) -> std::vector<std::uint32_t>
  {
    return spelled | std::views::transform([](char32_t code)
                       { return std::uint32_t{ code }; })
                   | std::ranges::to<std::vector>();
  }
  inline auto Walk(Bytes source, TextFormat format = { }) -> Walked
  {
    auto spelled{ std::vector<char32_t>{ } };
    auto const report{ DecodeResilient(source, std::back_inserter(spelled), format) };
    EXPECT_EQ(report.codepoints, spelled.size()) << "the report miscounted what it wrote";
    return { AsValues(spelled), report.replacements, source.size() };
  }
  inline constexpr auto UTF16_LE{ TextFormat{ Encoding::UTF16, std::endian::little } };
  inline constexpr auto UTF16_BE{ TextFormat{ Encoding::UTF16, std::endian::big    } };
}
