#pragma once

#include "oxbox/serialization/binary-reader.hpp"
#include "oxbox/serialization/binary-writer.hpp"

namespace oxbox::serialization::detail::format_positional
{
  struct PositionalFormat : binary_wire::BinaryWire
  {
    template <Sink S>
    using Writer = binary_writer::BinaryWriter<S, false>;

    template <Source Src>
    using Reader = binary_reader::BinaryReader<Src, false>;
  };
}

namespace oxbox::serialization
{
  using detail::format_positional::PositionalFormat;
}
