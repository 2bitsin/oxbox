#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <istream>
#include <span>
#include <streambuf>
#include <string_view>
#include <vector>

#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/errors.hpp"

// The plumbing between format backends and the streaming byte contracts
// (docs/streaming-io.md §2): drain loops for writers, and a std::istream
// facade for reader backends whose parsers consume standard streams.

namespace oxbox::serialization::detail
{
  // a full sink is an IoError here: a format's output is not optional
  template <Sink S>
  auto PutBytes(S& sink, std::span<std::byte const> bytes) -> void
  {
    while (!bytes.empty()) {
      if (sink.Write(bytes).empty())
        throw IoError{ "sink full with wire bytes still pending" };
    }
  }

  template <Sink S>
  auto PutText(S& sink, std::string_view text) -> void
  {
    PutBytes(sink, std::span<std::byte const>{
      std::bit_cast<std::byte const*>(text.data()), text.size() });
  }

  // for a format whose model is the contiguous wire itself; character
  // formats must not use this, they consume incrementally
  template <Source Src>
  auto DrainBytes(Src& source) -> std::vector<std::byte>
  {
    std::vector<std::byte> out;
    std::array<std::byte, 4096> window;   // NOLINT(*-magic-numbers)
    for (;;) {
      std::span<std::byte> space{ window };
      auto const got{ source.Read(space) };
      if (got.empty()) return out;
      out.insert(out.end(), got.begin(), got.end());
    }
  }

  // a std::streambuf over a Source, so parsers that take a std::istream
  // consume incrementally; this fixed window is the only raw-text buffer
  template <Source Src>
  class SourceBuf final : public std::streambuf
  {
  public:
    explicit SourceBuf(Src& source) : _source{ source } {}

  private:
    auto underflow() -> int_type override
    {
      if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
      std::span<std::byte> space{ _window };
      auto const got{ _source.Read(space) };
      if (got.empty()) return traits_type::eof();
      auto* const base{ std::bit_cast<char*>(_window.data()) };
      setg(base, base, base + got.size());
      return traits_type::to_int_type(*gptr());
    }

    Src&                        _source;
    std::array<std::byte, 4096> _window{};   // NOLINT(*-magic-numbers)
  };

  template <Source Src>
  class SourceStream
  {
  public:
    explicit SourceStream(Src& source) : _buf{ source }, _in{ &_buf } {}
    [[nodiscard]] auto Stream() -> std::istream& { return _in; }

  private:
    SourceBuf<Src> _buf;
    std::istream   _in;
  };
}
