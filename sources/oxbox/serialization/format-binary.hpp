#pragma once

#include "oxbox/serialization/binary-reader.hpp"
#include "oxbox/serialization/binary-writer.hpp"

namespace oxbox::serialization::detail::format_binary
{
  struct BinaryFormat : binary_wire::BinaryWire
  {
    template <Sink S>
    using Writer = binary_writer::BinaryWriter<S, true>;

    template <Source Src>
    using Reader = binary_reader::BinaryReader<Src, true>;
  };
}

namespace oxbox::serialization
{
  using detail::format_binary::BinaryFormat;
}
