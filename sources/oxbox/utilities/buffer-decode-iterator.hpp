#pragma once
// Walking a byte buffer as the code points it holds: an input iterator that
// reaches its sentinel where the bytes stop decoding.

#include "oxbox/utilities/codepoint-bytes.hpp"

#include <bit>
#include <cstddef>
#include <iterator>
#include <optional>

namespace oxbox::utilities::detail::buffer_decode_iterator
{
  using std::endian;
  using std::optional;

  using codepoint_bytes::ConstantBytes;

  struct BufferDecodeIterator 
  {
    using value_type        = char32_t;
    using reference         = value_type const&;
    using iterator_concept  = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using sentinel          = std::default_sentinel_t;
    constexpr BufferDecodeIterator() noexcept = default;
    constexpr BufferDecodeIterator(ConstantBytes& source_buffer , 
                                   Encoding       text_encoding = Encoding::UTF8, 
                                   endian         source_endian = endian::native) 
                                   noexcept
    : _source_buffer{ &source_buffer  }
    , _text_encoding{  text_encoding  }
    , _source_endian{  source_endian  }    
    , _current_value{ DecodeFromBytes<value_type>(
                       *_source_buffer,
                        _text_encoding,
                        _source_endian) } 
    { }
    using Self = BufferDecodeIterator;
    constexpr auto operator = (Self const&)  -> Self& = default;
    constexpr auto operator = (Self &&) noexcept -> Self& = default;
    // C++20 rewrites `a != b` as `!(a == b)` and synthesises the reversed
    // argument order, so this one operator is all four spellings.
    friend constexpr auto operator == (Self const& lhs, sentinel const&) noexcept
      -> bool
    { return !lhs._current_value; }
    constexpr auto operator * () 
      const noexcept -> reference { 
        return *_current_value; }
    constexpr auto operator ++ ()  noexcept -> Self& {
      if (!_source_buffer) return *this;
      _current_value = DecodeFromBytes<value_type>(
                        *_source_buffer,
                         _text_encoding,
                         _source_endian);
      return *this; 
    }
  private:
    ConstantBytes*        _source_buffer{ nullptr           };
    Encoding              _text_encoding{ Encoding::UTF8    };
    endian                _source_endian{ endian::native    };    
    optional<value_type>  _current_value{                   };
  };
}

namespace oxbox::utilities
{
  using detail::buffer_decode_iterator::BufferDecodeIterator;
}
