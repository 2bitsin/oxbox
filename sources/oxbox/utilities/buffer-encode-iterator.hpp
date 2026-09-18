#pragma once
// Writing code points into a byte buffer: an output iterator whose refusal
// latches, and which reports the bytes it was short of.

#include "oxbox/utilities/codepoint-bytes.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace oxbox::utilities::detail::buffer_encode_iterator
{
  using std::endian;

  struct BufferEncodeIterator
  {
    using value_type = char32_t;
  private:
    struct _EncodeIntoBytes
    {
      constexpr _EncodeIntoBytes(BufferEncodeIterator& self)
        noexcept: _this{ self } { }
      // A refusal latches: every later write is refused too, or a narrower
      // codepoint would fill the room the refused one left and truncate.
      constexpr auto operator = (value_type value)
        const noexcept -> _EncodeIntoBytes const&
      {
        if (_this._overflow_size != 0
         || _this._target_buffer == nullptr)
        { _this._overflow_size += _NeededBytes(value);
          return *this; }

        auto next_position{ *_this._target_buffer };

        auto const payload_size{
          EncodeIntoBytes(value               ,
                          next_position       ,
                          _this._text_encoding,
                          _this._target_endian) };

        // 0 is a codepoint no room would ever have taken, so nothing was short
        if (payload_size <= 0) {
          _this._overflow_size = -payload_size;
          return *this; }

        *_this._target_buffer = next_position;
        return *this;
      }
    private:
      // the encoders answer a short buffer with (room - needed), so an empty
      // span makes them measure
      constexpr auto _NeededBytes(value_type value)
        const noexcept -> std::intptr_t
      {
        auto nowhere{ WritableBytes{ } };
        return -EncodeIntoBytes(value               ,
                                nowhere             ,
                                _this._text_encoding,
                                _this._target_endian);
      }
      BufferEncodeIterator& _this;
    };
  public:
    // The proxy is made on dereference: held, its reference member would
    // delete the default constructor and both assignments the typedefs claim.
    using reference         = _EncodeIntoBytes;
    using difference_type   = std::ptrdiff_t;
    using iterator_concept  = std::output_iterator_tag;
    using iterator_category = std::output_iterator_tag;
    using sentinel          = std::default_sentinel_t;
    constexpr BufferEncodeIterator() noexcept = default;
    constexpr BufferEncodeIterator(WritableBytes& target_buffer                 ,
                                   Encoding       text_encoding = Encoding::UTF8,
                                   std::endian    target_endian = endian::native)
                                   noexcept
    : _target_buffer{ &target_buffer }
    , _text_encoding{  text_encoding }
    , _target_endian{  target_endian }
    , _overflow_size{  0             }
    { }
    using Self = BufferEncodeIterator;
    constexpr BufferEncodeIterator(Self const&) = default;
    constexpr BufferEncodeIterator(Self &&) noexcept = default;
    constexpr auto operator = (Self const&)  -> Self& = default;
    constexpr auto operator = (Self &&) noexcept -> Self& = default;
    friend constexpr auto operator == (Self const& lhs, sentinel const&)
      noexcept -> bool
    {
      return lhs._target_buffer == nullptr
          || lhs._target_buffer->empty()
          || lhs._overflow_size != 0;
    }
    constexpr auto operator * () noexcept -> reference {
      return _EncodeIntoBytes{ *this };
    }
    // The write publishes the caller's new position, so ++ has nothing to do.
    constexpr auto operator ++ () noexcept -> Self& {
      return *this;
    }
    // Self& and not a copy: `*it++ = v` through a temporary would record the
    // refusal on the temporary and lose the latch.
    constexpr auto operator ++ (int) noexcept -> Self& {
      return ++*this;
    }
    // The extra room this sink would have needed on top of the span it was
    // given, which the sentinel does not answer: a full buffer is at its end too.
    constexpr auto ShortfallBytes() const noexcept -> std::size_t {
      return static_cast<std::size_t>(_overflow_size);
    }
  private:
    WritableBytes*    _target_buffer{ nullptr        };
    Encoding          _text_encoding{ Encoding::UTF8 };
    std::endian       _target_endian{ endian::native };
    std::intptr_t     _overflow_size{ 0              };

  };

  static_assert(std::output_iterator<BufferEncodeIterator, char32_t>,
    "BufferEncodeIterator says output_iterator_tag: it must satisfy "
    "std::output_iterator -- movable, default-initializable, *it = cp, "
    "++it and *it++ = cp.");
  static_assert(std::default_initializable<BufferEncodeIterator>);
  static_assert(std::copyable<BufferEncodeIterator>);
}

namespace oxbox::utilities
{
  using detail::buffer_encode_iterator::BufferEncodeIterator;
}
