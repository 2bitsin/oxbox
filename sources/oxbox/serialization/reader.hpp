#pragma once

#include <array>
#include <istream>
#include <memory>
#include <optional>
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

namespace oxbox::serialization::detail::reader
{
  template <typename _VTy>
  struct BasicReader
  {
    virtual ~BasicReader() = default;
    virtual auto Read(std::istream& source, _VTy& record) -> bool = 0;
  };

// 
  inline auto ReadDelimited(std::istream& source, std::string_view delimiter)
    -> std::optional<std::string>
  {
    std::string text;
    for (char ch{}; source.get(ch); )
    {
      text.push_back(ch);
      if (text.ends_with(delimiter))
      {
        text.resize(text.size() - delimiter.size());
        return text;
      }
    }
    if (text.empty())
      return std::nullopt;
    return text;
  }

// 
  template <typename _V>
  struct ReadTraits
  {
    using Value = _V;

    template <typename _Format>
    static auto Delimiter() -> std::string_view
    {
      return FormatTraits<_Format>::separator;
    }

    template <typename _Format>
    static auto Body(std::string_view text, _V& out) -> void
    {
      out = Deserialize<_Format, _V>(text);
    }
  };

  template <oxbox::utilities::FixedString DELIM>
  struct ReadTraits<DelimitedString<DELIM>>
  {
  private:
    using _CharT = typename decltype(DELIM)::CharType;
    static_assert(std::is_same_v<_CharT, char>,
      "DelimitedString currently supports only char delimiters — wider "
      "char types need a wide-stream BasicReader.");

  public:
    using Value = std::basic_string<_CharT>;

    template <typename /*_Format*/>
    static auto Delimiter() -> std::string_view
    {
      return DELIM.view();
    }

    template <typename /*_Format*/>
    static auto Body(std::string_view text, Value& out) -> void
    {
      out.assign(text);
    }
  };

// 
  template <typename _Format, typename _V>
  struct ReaderLeaf
    : public virtual BasicReader<typename ReadTraits<_V>::Value>
  {
    using _Traits = ReadTraits<_V>;
    using _Value  = typename _Traits::Value;

    auto Read(std::istream& source, _Value& out) -> bool override
    {
      auto text{ ReadDelimited(source,
                               _Traits::template Delimiter<_Format>()) };
      if (!text)
        return false;
      _Traits::template Body<_Format>(*text, out);
      return true;
    }
  };

  // Pairwise-distinct helper — catches duplicate value types in a pack.
  template <typename...>
  struct _AllDistinct : std::true_type {};

  template <typename _H, typename... _T>
  struct _AllDistinct<_H, _T...>
    : std::bool_constant<((!std::is_same_v<_H, _T>) && ...)
                         && _AllDistinct<_T...>::value> {};

  template <typename... _Ts>
  inline constexpr bool _all_distinct_v = _AllDistinct<_Ts...>::value;

  template <typename... _V>
  struct MultiReader
    : public virtual BasicReader<typename ReadTraits<_V>::Value>...
  {
    static_assert(_all_distinct_v<typename ReadTraits<_V>::Value...>,
      "Reader pack has two elements yielding the same value type — "
      "at most one DelimitedString per char type, and a DelimitedString's "
      "value type may not also appear as a separate record type.");
    using BasicReader<typename ReadTraits<_V>::Value>::Read...;
  };

  template <typename... _V>
  struct PickReader;

  template <typename _V>
  struct PickReader<_V>
    : std::type_identity<BasicReader<typename ReadTraits<_V>::Value>> {};

  template <typename _First, typename... _Rest>
  struct PickReader<_First, _Rest...>
    : std::type_identity<MultiReader<_First, _Rest...>> {};

  template <typename... _V>
  using PickReaderT = typename PickReader<_V...>::type;

  template <typename... _V>
  using PickReaderPtr = std::unique_ptr<PickReaderT<_V...>>;

  template <typename _Format, typename... _V>
  struct MultiReaderImpl
    : public MultiReader<_V...>
    , public ReaderLeaf<_Format, _V>... {};

  template <typename _Format, typename... _V>
  inline auto ReaderFactory() -> PickReaderPtr<_V...>
  {
    if constexpr (sizeof...(_V) > 1) {
      return std::make_unique<MultiReaderImpl<_Format, _V...>>();
    } else {
      return std::make_unique<ReaderLeaf<_Format, _V...>>();
    }
  }

// 
  class Reader
  {
  public:
    static auto Formats() -> std::span<std::string_view const>
    {
      return AllFormats::Names();
    }

    template <typename... _V>
    static auto Make(std::string_view format) -> PickReaderPtr<_V...>
    {
      auto result = AllFormats::Visit(format, [&]<typename _F>(_F) {
        return ReaderFactory<_F, _V...>();
      });
      if (!result) return nullptr;
      return std::move(*result);
    }
  };
}


namespace oxbox::serialization
{
  using detail::reader::Reader;
  using detail::reader::BasicReader;
  using detail::reader::MultiReader;
  using detail::reader::PickReaderT;
  using detail::reader::PickReaderPtr;
  using detail::reader::ReadTraits;
}
