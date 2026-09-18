#pragma once

#include <_buildutil/reflect.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace oxbox::serialization
{

  // Sink and Source (docs/streaming-io.md §2): the caller's span is in-out
  // and is advanced past the transferred region; an empty return means end
  // of stream or sink full, not an error, and I/O errors throw.

  template <typename S>
  concept Sink = requires(S& s, std::span<std::byte const>& data) {
    { s.Write(data) } -> std::same_as<std::span<std::byte const>>;
  };

  template <typename S>
  concept Source = requires(S& s, std::span<std::byte>& buffer) {
    { s.Read(buffer) } -> std::same_as<std::span<std::byte const>>;
  };

  template <typename C>
  concept Character = std::same_as<C, char> || std::same_as<C, wchar_t>
    || std::same_as<C, char8_t> || std::same_as<C, char16_t>
    || std::same_as<C, char32_t>;

  namespace detail
  {
    // void when the element is not string-like; Character<void> then fails
    template <typename E>
    consteval auto _SequenceChar() {
      if constexpr      (std::convertible_to<E, std::string_view>)    return char{};
      else if constexpr (std::convertible_to<E, std::wstring_view>)   return wchar_t{};
      else if constexpr (std::convertible_to<E, std::u8string_view>)  return char8_t{};
      else if constexpr (std::convertible_to<E, std::u16string_view>) return char16_t{};
      else if constexpr (std::convertible_to<E, std::u32string_view>) return char32_t{};
    }
  }

  template <typename R>
  using SequenceCharT =
    decltype(detail::_SequenceChar<std::ranges::range_reference_t<R>>());

  template <typename R, typename C>
  concept StringSequenceOf = std::ranges::input_range<R> && Character<C>
    && std::convertible_to<std::ranges::range_reference_t<R>,
                           std::basic_string_view<C>>;

  template <typename R>
  concept StringSequence = std::ranges::input_range<R>
    && Character<SequenceCharT<R>>;

  namespace detail
  {
    // the stand-in for a Reader/Writer's stream parameter when no real stream
    // is involved: the Format concept's probe, and readers built from a node
    struct ProbeSink {
      auto Write(std::span<std::byte const>& data) -> std::span<std::byte const>
      { auto const written{ data }; data = data.subspan(data.size()); return written; }
    };
    struct ProbeSource {
      auto Read(std::span<std::byte>&) -> std::span<std::byte const> { return {}; }
    };
  }


  template <typename T> concept HasMemberArchive = requires(T& t) { t._Archive(); };
  template <typename T> concept HasAdlArchive    = requires(T& t) { _Archive(t);  };

  template <typename T> concept HasMemberRestore = requires(T& t) {
    { t._Restore() } -> std::same_as<void>;
  };
  template <typename T> concept HasAdlRestore = requires(T& t) {
    { _Restore(t) } -> std::same_as<void>;
  };

  template <typename T> concept HasArchiveHook = HasMemberArchive<T> || HasAdlArchive<T>;
  template <typename T> concept HasRestoreHook = HasMemberRestore<T> || HasAdlRestore<T>;

  template <typename V> concept HasVariantRestore = requires(V const& v) {
    { _Restore(v) } -> std::same_as<V>;
  };

  template <typename T>
  concept HasMemberEncode = requires(T const& t) { t._Encode(); };

  template <typename T>
  concept HasAdlEncode = requires(T const& t) { _Encode(t); };

  template <typename T>
  concept HasEncode = HasMemberEncode<T> || HasAdlEncode<T>;

  template <HasEncode T>
  constexpr auto WireOf(T const& t)
  {
    if constexpr (HasMemberEncode<T>) return t._Encode();
    else                              return _Encode(t);
  }

  template <typename T>
  using WireTypeOf = std::remove_cvref_t<decltype(WireOf(std::declval<T const&>()))>;

  template <typename T>
  concept HasMemberDecode = HasEncode<T> && requires(WireTypeOf<T> w) {
    { T::_Decode(std::move(w)) } -> std::same_as<T>;
  };

  template <typename T>
  concept HasAdlDecode = HasEncode<T> && requires(WireTypeOf<T> w) {
    { _Decode(std::type_identity<T>{}, std::move(w)) } -> std::same_as<T>;
  };

  template <typename T>
  concept HasDecode = HasMemberDecode<T> || HasAdlDecode<T>;

  template <typename T>
  concept HasEncodeDecode = HasEncode<T> && HasDecode<T>;


  // `reflect::reflected<T>` is true only when the tag is there and a
  // definition exists, so a project without the reflect extension reports
  // "not reflected" rather than an empty scheme.

  namespace detail
  {
    // the box tells a class from an enum, which `reflect::reflected` alone
    // does not: class_scheme, or derived_scheme when bases were captured
    template <typename>          struct IsClassSchemeT : std::false_type {};
    template <typename... Items> struct IsClassSchemeT<::reflect::class_scheme<Items...>>
      : std::true_type {};
    template <typename Bases, typename... Items>
    struct IsClassSchemeT<::reflect::derived_scheme<Bases, Items...>>
      : std::true_type {};

    template <typename>          struct IsEnumSchemeT : std::false_type {};
    template <typename... Items> struct IsEnumSchemeT<::reflect::enum_scheme<Items...>>
      : std::true_type {};

    // the raw ADL call and not reflect::scheme_of<T>(), which static_asserts
    // on an unreflected type where a concept needs a plain substitution failure
    template <typename T>
    using ReflectedSchemeT =
      std::remove_cvref_t<decltype(reflect_scheme(static_cast<T*>(nullptr)))>;

    template <typename T, typename M>
    constexpr auto _MemberOwner(M T::*) -> std::type_identity<T>;

    // the generator emits an accessor lambda for a reference member, which
    // `_MemberOwner` cannot deduce against ([dcl.mptr]/3): ask this first
    template <typename Item>
    inline constexpr bool _IS_POINTER_ITEM   // NOLINT(readability-identifier-naming)
      { std::is_member_object_pointer_v<decltype(Item::REFERENCE)> };

    // an accessor item passes: it names no owner, and the tier drops it anyway
    template <typename Owner, typename Item>
    consteval auto _ItemIsOwned() -> bool
    {
      if constexpr (_IS_POINTER_ITEM<Item>)
        return std::same_as<
          typename decltype(_MemberOwner(Item::REFERENCE))::type, Owner>;
      else
        return true;
    }

    // A derived class that never opted in still answers reflect_scheme through
    // ADL, so every member the scheme names has to belong to T itself.
    template <typename Owner, typename... Items>
    consteval auto _OwnsEveryItem(::reflect::class_scheme<Items...>) -> bool
    {
      return (_ItemIsOwned<Owner, Items>() && ...);
    }

    template <typename Owner, typename Bases, typename... Items>
    consteval auto _OwnsEveryItem(::reflect::derived_scheme<Bases, Items...>)
      -> bool
    {
      return (_ItemIsOwned<Owner, Items>() && ...);
    }
  }

  template <typename T>
  concept HasReflectedScheme = (!std::is_enum_v<T>)
    && ::reflect::reflected<T>
    && detail::IsClassSchemeT<detail::ReflectedSchemeT<T>>::value
    && detail::_OwnsEveryItem<T>(detail::ReflectedSchemeT<T>{});

  template <typename T>
  concept HasReflectedEnumMap = std::is_enum_v<T>
    && ::reflect::reflected<T>
    && detail::IsEnumSchemeT<detail::ReflectedSchemeT<T>>::value;


  template <typename T>
  concept HasScheme = HasReflectedScheme<T>;

  template <typename T>
  concept HasEnumMap = HasReflectedEnumMap<T>;

  template <typename T, typename W>
  concept WriteNativeCapable = requires(W& w, T const& value) { w.WriteNative(value); };

  template <typename T, typename R>
  concept ReadNativeCapable = requires(R& r) {
    { r.template ReadNative<T>() } -> std::same_as<T>;
  };

  template <typename W>
  concept WriterBackend = requires(W& w, std::string_view sv) {
    w.Write(false);
    w.Write(std::int64_t{});
    w.Write(std::uint64_t{});
    w.Write(double{});
    w.Write(sv);
    w.WriteNull();

    w.BeginObject();
    w.Field(sv);
    w.EndObject();

    w.BeginArray();
    w.EndArray();

    w.Flush();
  };

  template <typename R>
  concept ReaderBackend = requires(R& r, std::string_view sv) {
    { r.template Read<bool>()          } -> std::same_as<bool>;
    { r.template Read<std::int64_t>()  } -> std::same_as<std::int64_t>;
    { r.template Read<std::uint64_t>() } -> std::same_as<std::uint64_t>;
    { r.template Read<double>()        } -> std::same_as<double>;
    { r.template Read<std::string>()   } -> std::same_as<std::string>;
    { r.IsNull()                       } -> std::same_as<bool>;

    // Format-native subtree snapshot for the framework's Canned dispatch.
    r.Subtree();

    r.EnterObject();
    { r.HasField(sv) } -> std::same_as<bool>;
    { r.FieldNames() } -> std::ranges::range;
    r.EnterField(sv);
    r.LeaveField();
    r.LeaveObject();

    r.EnterArray();
    { r.HasNext() } -> std::same_as<bool>;
    r.EnterNext();
    r.LeaveNext();
    r.LeaveArray();

    { r.Path() } -> std::convertible_to<std::filesystem::path const&>;
  };

  template <typename F>
  concept Format = requires {
    typename F::template Writer<detail::ProbeSink>;
    typename F::template Reader<detail::ProbeSource>;
  };
}
