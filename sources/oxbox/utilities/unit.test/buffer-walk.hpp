#pragma once
// Walking a byte buffer as codepoints, for both iterators' tests.

#include "oxbox/utilities/buffer-decode-iterator.hpp"

#include <bit>
#include <cstdint>
#include <vector>

namespace oxbox::utilities::test
{
  // holds a reference to the span, so `source` is consumed as it goes
  inline auto Walk(Bytes& source, Encoding encoding, std::endian order)
    -> std::vector<std::uint32_t>
  {
    using Sentinel = BufferDecodeIterator::sentinel;
    auto spelled{ std::vector<std::uint32_t>{ } };
    for (BufferDecodeIterator it{ source, encoding, order }; it != Sentinel{ }; ++it)
      { spelled.push_back(std::uint32_t{ *it }); }
    return spelled;
  }
}
