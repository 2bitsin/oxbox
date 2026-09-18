#pragma once

// Walking a scheme against a reader backend: one Visit overload per shape,
// each of them the read half of what write-walker.hpp put on the wire.

#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "oxbox/serialization/concepts.hpp"
#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/hooks.hpp"
#include "oxbox/serialization/reflected-scheme.hpp"
#include "oxbox/serialization/scheme.hpp"
#include "oxbox/utilities/path.hpp"

namespace oxbox::serialization::detail
{
  using utilities::PathFromString;

  template <ReaderBackend R>
  class ReadWalker
  {
  public:
    explicit ReadWalker(R& r) noexcept : _r{r} {}

    template <typename T>
    auto operator()(T& out) -> void
    {
      if constexpr (ReadNativeCapable<T, R>) out = _r.template ReadNative<T>();
      else                                   Visit(out);
    }

    template <typename T>
      requires HasEncodeDecode<T>
    auto Visit(T& out) -> void {
      WireTypeOf<T> wire{};
      (*this)(wire);
      if constexpr (HasMemberDecode<T>) {
        out = T::_Decode(std::move(wire));
      } else {
        out = _Decode(std::type_identity<T>{}, std::move(wire));
      }
    }

    template <typename T>
      requires IsCanned<T>
    auto Visit(T& out) -> void {
      out = T{_r.Subtree()};
    }

    auto Visit(bool& out)        -> void { out = _r.template Read<bool>(); }
    auto Visit(std::string& out) -> void { out = _r.template Read<std::string>(); }

    auto Visit(std::filesystem::path& out) -> void {
      out = PathFromString(_r.template Read<std::string>());
    }

    template <std::integral T>
      requires (!std::same_as<T, bool>)
    auto Visit(T& out) -> void {
      if constexpr (std::signed_integral<T>)
        out = static_cast<T>(_r.template Read<std::int64_t>());
      else
        out = static_cast<T>(_r.template Read<std::uint64_t>());
    }

    template <std::floating_point T>
    auto Visit(T& out) -> void {
      out = static_cast<T>(_r.template Read<double>());
    }

    template <typename T>
    auto Visit(std::optional<T>& out) -> void {
      if (_r.IsNull()) { out.reset(); return; }
      T tmp{};
      (*this)(tmp);
      out = std::move(tmp);
    }

    // pointers read like optionals but write an explicit null, so on the wire
    // absence means optional and null means pointer
    template <typename T>
    auto Visit(std::unique_ptr<T>& out) -> void {
      if (_r.IsNull()) { out.reset(); return; }
      out = std::make_unique<T>();
      (*this)(*out);
    }

    template <typename T>
    auto Visit(std::shared_ptr<T>& out) -> void {
      if (_r.IsNull()) { out.reset(); return; }
      auto tmp = std::make_shared<T>();
      (*this)(*tmp);
      out = std::move(tmp);
    }

    // a short wire array is tolerated exactly where a scheme tolerates an
    // absent field: an optional or pointer element past the end stays empty
    template <typename... Ts>
    auto Visit(std::tuple<Ts...>& out) -> void {
      _r.EnterArray();
      std::size_t index{ 0 };
      auto const element = [&](auto& el) {
        using E = std::remove_cvref_t<decltype(el)>;
        if (!_r.HasNext()) {
          if constexpr (!requires(E& slot) { slot = std::nullopt; }
                        && !requires(E& slot) { slot = nullptr; }) {
            throw oxbox::serialization::ParseError{ std::format(
              "tuple at {} is missing element {} of {}",
              _r.Path(), index, sizeof...(Ts)) };
          }
        } else {
          _r.EnterNext();
          (*this)(el);
          _r.LeaveNext();
        }
        ++index;
      };
      std::apply([&](auto&... els) { (element(els), ...); }, out);
      _r.LeaveArray();
    }

    // exactly N elements are read and a shorter wire array is malformed;
    // surplus wire elements are left unread
    template <FixedSequence T>
    auto Visit(T& out) -> void {
      _r.EnterArray();
      for (auto& element : out) {
        if (!_r.HasNext())
          throw oxbox::serialization::ParseError{ std::format(
            "expected {} array elements at {}",
            std::tuple_size<T>::value, _r.Path()) };
        _r.EnterNext();
        (*this)(element);
        _r.LeaveNext();
      }
      _r.LeaveArray();
    }

    template <typename T>
      requires requires(T& c, typename T::value_type v) { c.begin(); c.end(); typename T::value_type; }
            && (!std::same_as<T, std::string>)
            && (!std::same_as<T, std::string_view>)
            && (!requires { typename T::key_type; typename T::mapped_type; })
            && (!FixedSequence<T>)
    auto Visit(T& out) -> void {
      _r.EnterArray();
      out = T{};
      while (_r.HasNext()) {
        _r.EnterNext();
        typename T::value_type tmp{};
        (*this)(tmp);
        _r.LeaveNext();
        if constexpr (requires { out.push_back(tmp); }) out.push_back(std::move(tmp));
        else                                            out.insert(std::move(tmp));
      }
      _r.LeaveArray();
    }

    template <typename M>
      requires requires { typename M::key_type; typename M::mapped_type; }
            && (std::convertible_to<typename M::key_type, std::string_view>)
    auto Visit(M& out) -> void {
      _r.EnterObject();
      out = M{};
      for (auto name : _r.FieldNames()) {
        _r.EnterField(std::string_view{name});
        typename M::mapped_type tmp{};
        (*this)(tmp);
        _r.LeaveField();
        out.emplace(typename M::key_type{name}, std::move(tmp));
      }
      _r.LeaveObject();
    }

    template <typename M>
      requires requires { typename M::key_type; typename M::mapped_type; }
            && HasEncodeDecode<typename M::key_type>
            && std::convertible_to<WireTypeOf<typename M::key_type>,
                                   std::string_view>
    auto Visit(M& out) -> void {
      using K = typename M::key_type;
      _r.EnterObject();
      out = M{};
      for (auto name : _r.FieldNames()) {
        _r.EnterField(std::string_view{name});
        typename M::mapped_type tmp{};
        (*this)(tmp);
        _r.LeaveField();

        WireTypeOf<K> wire_key{name};
        if constexpr (HasMemberDecode<K>) {
          out.emplace(K::_Decode(std::move(wire_key)), std::move(tmp));
        } else {
          out.emplace(_Decode(std::type_identity<K>{}, std::move(wire_key)),
                      std::move(tmp));
        }
      }
      _r.LeaveObject();
    }

    template <typename T>
      requires HasScheme<T> && (!HasEncodeDecode<T>)
    auto Visit(T& out) -> void {
      _r.EnterObject();
      std::apply([&](auto const&... fs) {
        auto pull = [&](auto const& f) {
          using Value = typename std::remove_cvref_t<decltype(f)>::value_type;
          static_assert(std::is_default_constructible_v<Value>,
            "every scheme field must be default-constructible: the read "
            "walker fills a default-constructed slot before Set. Give the "
            "type a default constructor or wrap the field in std::optional.");
          std::string_view name{f.WireName()};
          if (!_r.HasField(name)) {
            // optionals and pointers tolerate absence, everything else is
            // required
            if constexpr (!requires(Value& slot) { slot = std::nullopt; }
                          && !requires(Value& slot) { slot = nullptr; }) {
              throw oxbox::serialization::MissingField{_r.Path() / std::filesystem::path{name}};
            }
          } else {
            _r.EnterField(name);
            Value tmp{ };
            (*this)(tmp);
            _r.LeaveField();
            f.Set(out, std::move(tmp));
          }
        };
        (pull(fs), ...);
      }, SchemeFor(out).fields);
      _r.LeaveObject();
    }

    template <typename T>
      requires HasEnumMap<T>
    auto Visit(T& out) -> void {
      auto const s = _r.template Read<std::string>();
      constexpr auto map = EnumMapFor<T>();
      auto const value = map.FromString(s);
      if (!value) throw oxbox::serialization::ParseError{
        std::format("unknown enum value at {}: {}", _r.Path(), s)};
      out = *value;
    }

    template <typename First, typename... Rest>
      requires (!IsCanned<std::variant<First, Rest...>>)
    auto Visit(std::variant<First, Rest...>& out) -> void {
      using V = std::variant<First, Rest...>;
      static_assert(HasVariantRestore<V>,
        "discriminated variant must have ADL _Restore(V const&) -> V "
        "in the namespace of one of its alternatives");

      std::visit([this](auto& alt) { (*this)(alt); }, out);

      std::bitset<sizeof...(Rest) + 1> seen;
      seen.set(out.index());
      for (;;) {
        auto const idx_before = out.index();
        out = _Restore(std::as_const(out));
        if (out.index() == idx_before) break;
        if (seen.test(out.index())) {
          throw oxbox::serialization::ParseError{
            std::format("cycle in _Restore at {}", _r.Path())};
        }
        seen.set(out.index());
        std::visit([this](auto& alt) { (*this)(alt); }, out);
      }
    }

  private:
    R& _r;
  };
}
