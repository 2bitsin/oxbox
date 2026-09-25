#pragma once
// One exception type per alias: a base, a checked format and its arguments,
// and an id that keeps two aliases of the same shape apart.

#include "oxbox/utilities/fixed-string.hpp"

#include <concepts>
#include <exception>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace oxbox::utilities::detail::exception
{
  // A child constructible from std::string is handed the text, and what() is what that constructor makes of it;
  // [std.exceptions] makes it the string itself only for the nine classes it defines.
  template <typename Base>
  concept MessageConstructible = (std::derived_from<Base, std::runtime_error>
                               || std::derived_from<Base, std::logic_error>)
                              && std::constructible_from<Base, std::string const&>;

  template <typename Base>
  concept ExceptionBase = std::derived_from<Base, std::exception>
                       && !std::is_final_v<Base>
                       && std::copy_constructible<Base>
                       && std::is_copy_assignable_v<Base>
                       && (MessageConstructible<Base> || std::default_initializable<Base>);

  // Shared and immutable: copying the text cannot throw, as [exception]/2 asks of an exception.
  template <ExceptionBase Base>
  class OwnedText : public Base
  {
  public:
    explicit OwnedText(std::string text)
    : _text{ std::make_shared<std::string const>(std::move(text)) }
    {
    }

    OwnedText(OwnedText const& other) noexcept(std::is_nothrow_copy_constructible_v<Base>) = default;

    // A move is a copy, so a moved-from exception still answers what().
    OwnedText(OwnedText&& other) noexcept(std::is_nothrow_copy_constructible_v<Base>)
    : OwnedText{ std::as_const(other) }
    {
    }

    ~OwnedText() override = default;

    auto operator=(OwnedText const& other) noexcept(std::is_nothrow_copy_assignable_v<Base>) -> OwnedText& = default;
    auto operator=(OwnedText&& other)      noexcept(std::is_nothrow_copy_assignable_v<Base>) -> OwnedText&
    {
      return *this = std::as_const(other);
    }

    // A base whose what() is final is a compile error here, not a refusal: no trait can see a final member.
    [[nodiscard]] auto what() const noexcept -> char const* override { return _text->c_str(); }

  private:
    std::shared_ptr<std::string const> _text;
  };

  template <typename Base>
  struct CarrierOf
  {
    using type = OwnedText<Base>;
  };

  template <MessageConstructible Base>
  struct CarrierOf<Base>
  {
    using type = Base;
  };

  template <ExceptionBase Base>
  using TextCarrier = typename CarrierOf<Base>::type;

  template <typename Format>
  concept NarrowFormat = std::same_as<typename Format::CharType, char>;

  template <auto ID, ExceptionBase Base, FixedString FORMAT, typename... Args>
    requires NarrowFormat<decltype(FORMAT)>
  class Exception : public TextCarrier<Base>
  {
  public:
    explicit Exception(Args... args)
    : TextCarrier<Base>{ std::format(FORMAT_STRING, args...) }
    {
    }

    static constexpr auto Id{ ID };

  private:
    static constexpr std::format_string<Args&...> FORMAT_STRING{ FORMAT.view() };

    // The initializer is instantiated only when used; this checks it wherever the type is completed.
    static_assert(FORMAT_STRING.get() == FORMAT.view());
  };
}

namespace oxbox::utilities
{
  using detail::exception::Exception;
}
