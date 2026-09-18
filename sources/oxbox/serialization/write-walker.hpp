#pragma once

// Walking a scheme against a writer backend: every supported shape is a
// Visit overload here, and absent-versus-null on the wire is settled here.

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/reflected-scheme.hpp"
#include "oxbox/serialization/scheme.hpp"
#include "oxbox/utilities/path.hpp"

namespace oxbox::serialization::detail
{
  template <WriterBackend W>
  class WriteWalker
  {
  public:
    explicit WriteWalker(W& w) noexcept : _w{w} {}

    template <typename T>
    auto operator()(T const& value) -> void
    {
      if constexpr (WriteNativeCapable<T, W>) _w.WriteNative(value);
      else                                    Visit(value);
    }

    template <typename T>
      requires HasEncodeDecode<T>
    auto Visit(T const& v) -> void {
      auto wire = WireOf(v);
      (*this)(wire);
    }

    auto Visit(bool v)               -> void { _w.Write(v); }
    auto Visit(std::string const& v) -> void { _w.Write(std::string_view{v}); }
    auto Visit(std::string_view v)   -> void { _w.Write(v); }

    auto Visit(std::filesystem::path const& path) -> void {      
      using namespace utilities;
      _w.Write(PathToString(path));
    }

    template <std::integral T>
      requires (!std::same_as<T, bool>)
    auto Visit(T v) -> void {
      if constexpr (std::signed_integral<T>) _w.Write(static_cast<std::int64_t>(v));
      else                                   _w.Write(static_cast<std::uint64_t>(v));
    }

    template <std::floating_point T>
    auto Visit(T v) -> void { _w.Write(static_cast<double>(v)); }

    template <typename T>
    auto Visit(std::optional<T> const& o) -> void {
      if (o.has_value()) (*this)(*o);
      else               _w.WriteNull();
    }

    template <typename T>
    auto Visit(std::unique_ptr<T> const& p) -> void {
      if (p) (*this)(*p);
      else   _w.WriteNull();
    }

    template <typename T>
    auto Visit(std::shared_ptr<T> const& p) -> void {
      if (p) (*this)(*p);
      else   _w.WriteNull();
    }

    template <typename T>
      requires requires(T const& c) { c.begin(); c.end(); typename T::value_type; }
            && (!std::same_as<T, std::string>)
            && (!std::same_as<T, std::string_view>)
            && (!requires { typename T::key_type; typename T::mapped_type; })
    auto Visit(T const& c) -> void {
      _w.BeginArray();
      for (auto const& el : c) (*this)(el);
      _w.EndArray();
    }

    template <typename M>
      requires requires { typename M::key_type; typename M::mapped_type; }
            && (std::convertible_to<typename M::key_type, std::string_view>)
    auto Visit(M const& m) -> void {
      _w.BeginObject();
      for (auto const& [k, v] : m) {
        _w.Field(std::string_view{k});
        (*this)(v);
      }
      _w.EndObject();
    }

    template <typename M>
      requires requires { typename M::key_type; typename M::mapped_type; }
            && HasEncodeDecode<typename M::key_type>
            && std::convertible_to<WireTypeOf<typename M::key_type>,
                                   std::string_view>
    auto Visit(M const& m) -> void {
      _w.BeginObject();
      for (auto const& [k, v] : m) {
        auto const wire = WireOf(k);
        _w.Field(std::string_view{wire});
        (*this)(v);
      }
      _w.EndObject();
    }

    template <typename T>
      requires HasScheme<T> && (!HasEncodeDecode<T>)
    auto Visit(T const& obj) -> void {
      _w.BeginObject();
      std::apply([&](auto const&... fs) {
        auto emit = [&](auto const& f) {
          using Value = typename std::remove_cvref_t<decltype(f)>::value_type;
          auto const put_name = [this, &f] {
            if constexpr (requires { _w.Field(f.name, f.spelling); })
              _w.Field(f.name, f.spelling);   // a format with conventions of its own
            else
              _w.Field(f.WireName());
          };
          // A nullopt field is absent; smart-pointer null means present but empty.
          // Positional formats must retain a null slot for absence so later fields keep their positions.
          if constexpr (IsStdOptionalT<Value>::value) {
            auto const& value = f.Get(obj);
            if (!value.has_value()) {
              if constexpr (requires { W::PRESERVE_FIELD_SLOTS; }) {
                if constexpr (!W::PRESERVE_FIELD_SLOTS) return;
              } else return;
            }
            put_name();
            (*this)(value);
          } else {
            put_name();
            (*this)(f.Get(obj));
          }
        };
        (emit(fs), ...);
      }, SchemeFor(obj).fields);
      _w.EndObject();
    }

    template <typename T>
      requires HasEnumMap<T>
    auto Visit(T const& v) -> void {
      constexpr auto map = EnumMapFor<T>();
      auto const s = map.ToString(v);
      if (!s) throw oxbox::serialization::InvalidArgument{
        "EnumMap::ToString",
        std::format("enum value has no wire mapping")};
      _w.Write(*s);
    }

    template <typename First, typename... Rest>
    auto Visit(std::variant<First, Rest...> const& v) -> void {
      std::visit([this](auto const& alt) { (*this)(alt); }, v);
    }

    template <typename... Ts>
    auto Visit(std::tuple<Ts...> const& t) -> void {
      _w.BeginArray();
      std::apply([this](auto const&... els) { ((*this)(els), ...); }, t);
      _w.EndArray();
    }

  private:
    W& _w;
  };
}
