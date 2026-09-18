#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "oxbox/serialization/delimited-string.hpp"
#include "oxbox/serialization/format-pack.hpp"
#include "oxbox/serialization/format-traits.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/utilities/fixed-string.hpp"

namespace oxbox::serialization::detail::writer
{
  enum class WriteOrderHint {
    FIRST, // First record of a stream; for records, no leading separator
    NEXT,  // Subsequent records mid-stream
    LAST   // Final record; for delimited slots, suppresses the trailing
           // delimiter. For record slots, equivalent to Next.
  };

  template <typename _VTy>
  struct BasicWriter
  {
    virtual ~BasicWriter() = default;
    virtual auto Write(std::ostream& sink, _VTy const& record,
      WriteOrderHint order_hint = WriteOrderHint::FIRST) -> void = 0;
  };

// 
  template <typename _V>
  struct WriteTraits
  {
    using Value = _V;

    template <typename _Format>
    static auto Prepend(WriteOrderHint hint) -> std::string_view
    {
      return hint == WriteOrderHint::FIRST
           ? std::string_view{}
           : FormatTraits<_Format>::separator;
    }

    template <typename /*_Format*/>
    static auto Append(WriteOrderHint /*hint*/) -> std::string_view
    {
      return {};
    }

    template <typename _Format>
    static auto Body(std::ostream& sink, _V const& v) -> void
    {
      SerializeTo<_Format>(v, sink);
    }
  };

  template <oxbox::utilities::FixedString DELIM>
  struct WriteTraits<DelimitedString<DELIM>>
  {
  private:
    using _CharT = typename decltype(DELIM)::CharType;
    static_assert(std::is_same_v<_CharT, char>,
      "DelimitedString currently supports only char delimiters — wider "
      "char types need a wide-stream BasicWriter.");

  public:
    using Value = std::basic_string<_CharT>;

    template <typename /*_Format*/>
    static auto Prepend(WriteOrderHint /*hint*/) -> std::string_view
    {
      return {};
    }

    template <typename /*_Format*/>
    static auto Append(WriteOrderHint hint) -> std::string_view
    {
      return hint == WriteOrderHint::LAST
           ? std::string_view{}
           : DELIM.view();
    }

    template <typename /*_Format*/>
    static auto Body(std::ostream& sink, Value const& v) -> void
    {
      sink.write(v.data(), static_cast<std::streamsize>(v.size()));
    }
  };

// 
  template <typename _Format, typename _V>
  struct WriterLeaf
    : public virtual BasicWriter<typename WriteTraits<_V>::Value>
  {
    using _Traits = WriteTraits<_V>;
    using _Value  = typename _Traits::Value;

    auto Write(std::ostream& sink, _Value const& v,
      WriteOrderHint hint) -> void override
    {
      auto const pre{ _Traits::template Prepend<_Format>(hint) };
      if (!pre.empty())
        sink.write(pre.data(), static_cast<std::streamsize>(pre.size()));

      _Traits::template Body<_Format>(sink, v);

      auto const post{ _Traits::template Append<_Format>(hint) };
      if (!post.empty())
        sink.write(post.data(), static_cast<std::streamsize>(post.size()));
    }
  };

// 
  template <typename...>
  struct _AllDistinct : std::true_type {};

  template <typename _H, typename... _T>
  struct _AllDistinct<_H, _T...>
    : std::bool_constant<((!std::is_same_v<_H, _T>) && ...)
                         && _AllDistinct<_T...>::value> {};

  template <typename... _Ts>
  inline constexpr bool _all_distinct_v = _AllDistinct<_Ts...>::value;

  template <typename... _V>
  struct MultiWriter
    : public virtual BasicWriter<typename WriteTraits<_V>::Value>...
  {
    static_assert(_all_distinct_v<typename WriteTraits<_V>::Value...>,
      "Writer pack has two elements yielding the same value type — "
      "at most one DelimitedString per char type, and a DelimitedString's "
      "value type may not also appear as a separate record type.");
    using BasicWriter<typename WriteTraits<_V>::Value>::Write...;
  };

  template <typename... _V>
  struct PickWriter;

  template <typename _V>
  struct PickWriter<_V>
    : std::type_identity<BasicWriter<typename WriteTraits<_V>::Value>> {};

  template <typename _First, typename... _Rest>
  struct PickWriter<_First, _Rest...>
    : std::type_identity<MultiWriter<_First, _Rest...>> {};

  template <typename... _V>
  using PickWriterT = typename PickWriter<_V...>::type;

  template <typename... _V>
  using PickWriterPtr = std::unique_ptr<PickWriterT<_V...>>;

  template <typename _Format, typename... _V>
  struct MultiWriterImpl
    : public MultiWriter<_V...>
    , public WriterLeaf<_Format, _V>... {};

  template <typename _Format, typename... _V>
  inline auto WriterFactory() -> PickWriterPtr<_V...>
  {
    if constexpr (sizeof...(_V) > 1) {
      return std::make_unique<MultiWriterImpl<_Format, _V...>>();
    } else {
      return std::make_unique<WriterLeaf<_Format, _V...>>();
    }
  }

// 
  struct Writer
  {
    static auto Formats() -> std::span<std::string_view const>
    {
      return AllFormats::Names();
    }

// 
    static auto FormatFromExtension(std::string_view ext)
      -> std::optional<std::string_view>
    {
      if (auto const v = AllFormats::FromExtension(ext))
        return AllFormats::NameOf(*v);
      return std::nullopt;
    }

// 
    static auto ExtensionFromFormat(std::string_view name)
      -> std::optional<std::string_view>
    {
      if (auto const v = AllFormats::FromName(name))
        return AllFormats::ExtensionOf(*v);
      return std::nullopt;
    }

    template <typename... _V>
    static auto Make(std::string_view format) -> PickWriterPtr<_V...>
    {
      auto result = AllFormats::Visit(format, [&]<typename _F>(_F) {
        return WriterFactory<_F, _V...>();
      });
      if (!result) return nullptr;
      return std::move(*result);
    }
  };
}

namespace oxbox::serialization
{
  using detail::writer::Writer;
  using detail::writer::BasicWriter;
  using detail::writer::MultiWriter;
  using detail::writer::PickWriterT;
  using detail::writer::PickWriterPtr;
  using detail::writer::WriteOrderHint;
  using detail::writer::WriteTraits;
}
