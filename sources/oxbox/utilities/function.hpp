#pragma once
// MoveOnlyFunction: std::move_only_function where <version> says the library
// has it, and a conforming fallback that owns its target on the heap, with no
// small-buffer optimisation, where it does not.

#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <version>

namespace oxbox::utilities::detail::function::fallback
{
  // [func.wrap.move.class]/1: the target as the signature's cv ref names it,
  // and as operator() invokes it (inv-quals).
  struct Unqualified
  {
    template <typename _Type> using Named   = _Type;
    template <typename _Type> using Invoked = _Type&;
  };

  struct Const
  {
    template <typename _Type> using Named   = _Type const;
    template <typename _Type> using Invoked = _Type const&;
  };

  struct LValue
  {
    template <typename _Type> using Named   = _Type&;
    template <typename _Type> using Invoked = _Type&;
  };

  struct ConstLValue
  {
    template <typename _Type> using Named   = _Type const&;
    template <typename _Type> using Invoked = _Type const&;
  };

  struct RValue
  {
    template <typename _Type> using Named   = _Type&&;
    template <typename _Type> using Invoked = _Type&&;
  };

  struct ConstRValue
  {
    template <typename _Type> using Named   = _Type const&&;
    template <typename _Type> using Invoked = _Type const&&;
  };

  template <typename _Result, bool NOEXCEPT, typename... _Args>
  class Target
  {
  public:
    Target()                                     = default;
    Target(Target const&)                        = delete;
    Target(Target&&)                             = delete;
    auto operator=(Target const&) -> Target&     = delete;
    auto operator=(Target&&) -> Target&          = delete;
    virtual ~Target()                            = default;

    virtual auto Invoke(_Args&&... args) noexcept(NOEXCEPT) -> _Result = 0;
  };

  template <typename _Callable, typename _Quals, typename _Result, bool NOEXCEPT, typename... _Args>
  class HeldTarget final : public Target<_Result, NOEXCEPT, _Args...>
  {
  public:
    template <typename... _Init>
    explicit HeldTarget(_Init&&... init)
    : _callable(std::forward<_Init>(init)...)
    { }

    auto Invoke(_Args&&... args) noexcept(NOEXCEPT) -> _Result override
    {
      using Invoked = typename _Quals::template Invoked<_Callable>;
      return std::invoke_r<_Result>(static_cast<Invoked>(_callable), std::forward<_Args>(args)...);
    }

  private:
    _Callable _callable;
  };

  template <typename _Quals, bool NOEXCEPT, typename _Callable, typename _Result, typename... _Args>
  concept CallableFrom = (NOEXCEPT
    ? std::is_nothrow_invocable_r_v<_Result, typename _Quals::template Named<_Callable>, _Args...>
      && std::is_nothrow_invocable_r_v<_Result, typename _Quals::template Invoked<_Callable>, _Args...>
    : std::is_invocable_r_v<_Result, typename _Quals::template Named<_Callable>, _Args...>
      && std::is_invocable_r_v<_Result, typename _Quals::template Invoked<_Callable>, _Args...>);

  template <typename _Type>
  inline constexpr bool IS_IN_PLACE_TYPE{ false };

  template <typename _Type>
  inline constexpr bool IS_IN_PLACE_TYPE<std::in_place_type_t<_Type>>{ true };

  template <typename _Signature>
  class MoveOnlyFunction;

  template <typename _Type>
  inline constexpr bool IS_MOVE_ONLY_FUNCTION{ false };

  template <typename _Signature>
  inline constexpr bool IS_MOVE_ONLY_FUNCTION<MoveOnlyFunction<_Signature>>{ true };

  // A concept, so the conjunction stops at the first false term: the
  // implicit members of a wrapper still being defined ask this of the wrapper.
  template <typename _Callable, typename _Self, typename _Owner, typename _Quals, bool NOEXCEPT, typename _Result,
            typename... _Args>
  concept Wraps = (!std::same_as<std::remove_cvref_t<_Callable>, _Self>)
               && (!std::same_as<std::remove_cvref_t<_Callable>, _Owner>)
               && (!IS_IN_PLACE_TYPE<std::remove_cvref_t<_Callable>>)
               && std::is_constructible_v<std::decay_t<_Callable>, _Callable>
               && CallableFrom<_Quals, NOEXCEPT, std::decay_t<_Callable>, _Result, _Args...>;

  // [func.wrap.move.ctor]/9: these three leave the wrapper empty when null.
  template <typename _Type>
  constexpr auto IsNull(_Type const& callable) noexcept -> bool
  {
    if constexpr (std::is_pointer_v<_Type> || std::is_member_pointer_v<_Type>)
      return callable == nullptr;
    else if constexpr (IS_MOVE_ONLY_FUNCTION<_Type>)
      return !callable;
    else
      return false;
  }

  // Everything but operator(), whose qualifiers each specialisation spells.
  template <typename _Self, typename _Quals, typename _Result, bool NOEXCEPT, typename... _Args>
  class Owner
  {
    using Held = Target<_Result, NOEXCEPT, _Args...>;

    template <typename _Callable>
    using HeldAs = HeldTarget<_Callable, _Quals, _Result, NOEXCEPT, _Args...>;

    template <typename _Callable>
    static constexpr bool ACCEPTS{ CallableFrom<_Quals, NOEXCEPT, _Callable, _Result, _Args...> };

    template <typename _Callable>
    static constexpr bool WRAPS{ Wraps<_Callable, _Self, Owner, _Quals, NOEXCEPT, _Result, _Args...> };

  public:
    using result_type = _Result;

    Owner() noexcept = default;

    Owner(std::nullptr_t) noexcept // NOLINT(google-explicit-constructor): implicit, as std's
    { }

    template <typename _Callable>
      requires WRAPS<_Callable>
    Owner(_Callable&& callable) // NOLINT(google-explicit-constructor): implicit, as std's
    {
      if (!IsNull(callable))
        _target = std::make_unique<HeldAs<std::decay_t<_Callable>>>(std::forward<_Callable>(callable));
    }

    template <typename _Callable, typename... _Init>
      requires std::constructible_from<_Callable, _Init...> && ACCEPTS<_Callable>
    explicit Owner(std::in_place_type_t<_Callable> /*tag*/, _Init&&... init)
    : _target{ std::make_unique<HeldAs<_Callable>>(std::forward<_Init>(init)...) }
    { }

    template <typename _Callable, typename _Element, typename... _Init>
      requires std::constructible_from<_Callable, std::initializer_list<_Element>&, _Init...>
            && ACCEPTS<_Callable>
    explicit Owner(std::in_place_type_t<_Callable> /*tag*/, std::initializer_list<_Element> list,
                   _Init&&... init)
    : _target{ std::make_unique<HeldAs<_Callable>>(list, std::forward<_Init>(init)...) }
    { }

    auto operator=(std::nullptr_t) noexcept -> _Self&
    {
      _target.reset();
      return Self();
    }

    template <typename _Callable>
      requires WRAPS<_Callable>
    auto operator=(_Callable&& callable) -> _Self&
    {
      _Self{ std::forward<_Callable>(callable) }.swap(Self());
      return Self();
    }

    auto swap(_Self& other) noexcept -> void { _target.swap(other._target); }

    explicit operator bool() const noexcept { return _target != nullptr; }

  protected:
    // [func.wrap.move.inv]/1: calling an empty wrapper is undefined, as in std.
    auto Call(_Args&&... args) const noexcept(NOEXCEPT) -> _Result
    { return _target->Invoke(std::forward<_Args>(args)...); }

  private:
    auto Self() noexcept -> _Self& { return static_cast<_Self&>(*this); }

    std::unique_ptr<Held> _target;
  };

  // Namespace scope, not hidden friends of Owner: cl 19.51 counts the friends
  // of two Owner instantiations as one function defined twice.
  template <typename _Signature>
  auto swap(MoveOnlyFunction<_Signature>& left, MoveOnlyFunction<_Signature>& right) noexcept -> void
  { left.swap(right); }

  template <typename _Signature>
  auto operator==(MoveOnlyFunction<_Signature> const& function, std::nullptr_t /*null*/) noexcept -> bool
  { return !function; }

  template <bool NOEXCEPT, typename _Result, typename... _Args>
  class MoveOnlyFunction<_Result(_Args...) noexcept(NOEXCEPT)>
  : public Owner<MoveOnlyFunction<_Result(_Args...) noexcept(NOEXCEPT)>, Unqualified, _Result, NOEXCEPT, _Args...>
  {
    using Base = Owner<MoveOnlyFunction<_Result(_Args...) noexcept(NOEXCEPT)>,
                       Unqualified, _Result, NOEXCEPT, _Args...>;

  public:
    using Base::Base;
    using Base::operator=;

    auto operator()(_Args... args) noexcept(NOEXCEPT) -> _Result
    { return this->Call(std::forward<_Args>(args)...); }
  };

  template <bool NOEXCEPT, typename _Result, typename... _Args>
  class MoveOnlyFunction<_Result(_Args...) const noexcept(NOEXCEPT)>
  : public Owner<MoveOnlyFunction<_Result(_Args...) const noexcept(NOEXCEPT)>, Const, _Result, NOEXCEPT, _Args...>
  {
    using Base = Owner<MoveOnlyFunction<_Result(_Args...) const noexcept(NOEXCEPT)>,
                       Const, _Result, NOEXCEPT, _Args...>;

  public:
    using Base::Base;
    using Base::operator=;

    auto operator()(_Args... args) const noexcept(NOEXCEPT) -> _Result
    { return this->Call(std::forward<_Args>(args)...); }
  };

  template <bool NOEXCEPT, typename _Result, typename... _Args>
  class MoveOnlyFunction<_Result(_Args...) & noexcept(NOEXCEPT)>
  : public Owner<MoveOnlyFunction<_Result(_Args...) & noexcept(NOEXCEPT)>, LValue, _Result, NOEXCEPT, _Args...>
  {
    using Base = Owner<MoveOnlyFunction<_Result(_Args...) & noexcept(NOEXCEPT)>, LValue, _Result, NOEXCEPT, _Args...>;

  public:
    using Base::Base;
    using Base::operator=;

    auto operator()(_Args... args) & noexcept(NOEXCEPT) -> _Result
    { return this->Call(std::forward<_Args>(args)...); }
  };

  template <bool NOEXCEPT, typename _Result, typename... _Args>
  class MoveOnlyFunction<_Result(_Args...) const & noexcept(NOEXCEPT)>
  : public Owner<MoveOnlyFunction<_Result(_Args...) const & noexcept(NOEXCEPT)>, ConstLValue, _Result, NOEXCEPT,
                 _Args...>
  {
    using Base = Owner<MoveOnlyFunction<_Result(_Args...) const & noexcept(NOEXCEPT)>,
                       ConstLValue, _Result, NOEXCEPT, _Args...>;

  public:
    using Base::Base;
    using Base::operator=;

    auto operator()(_Args... args) const & noexcept(NOEXCEPT) -> _Result
    { return this->Call(std::forward<_Args>(args)...); }
  };

  template <bool NOEXCEPT, typename _Result, typename... _Args>
  class MoveOnlyFunction<_Result(_Args...) && noexcept(NOEXCEPT)>
  : public Owner<MoveOnlyFunction<_Result(_Args...) && noexcept(NOEXCEPT)>, RValue, _Result, NOEXCEPT, _Args...>
  {
    using Base = Owner<MoveOnlyFunction<_Result(_Args...) && noexcept(NOEXCEPT)>, RValue, _Result, NOEXCEPT, _Args...>;

  public:
    using Base::Base;
    using Base::operator=;

    auto operator()(_Args... args) && noexcept(NOEXCEPT) -> _Result
    { return this->Call(std::forward<_Args>(args)...); }
  };

  template <bool NOEXCEPT, typename _Result, typename... _Args>
  class MoveOnlyFunction<_Result(_Args...) const && noexcept(NOEXCEPT)>
  : public Owner<MoveOnlyFunction<_Result(_Args...) const && noexcept(NOEXCEPT)>, ConstRValue, _Result, NOEXCEPT,
                 _Args...>
  {
    using Base = Owner<MoveOnlyFunction<_Result(_Args...) const && noexcept(NOEXCEPT)>,
                       ConstRValue, _Result, NOEXCEPT, _Args...>;

  public:
    using Base::Base;
    using Base::operator=;

    auto operator()(_Args... args) const && noexcept(NOEXCEPT) -> _Result
    { return this->Call(std::forward<_Args>(args)...); }
  };
}

namespace oxbox::utilities::detail::function
{
#ifdef __cpp_lib_move_only_function
  template <typename _Signature>
  using MoveOnlyFunction = std::move_only_function<_Signature>;
#else
  template <typename _Signature>
  using MoveOnlyFunction = fallback::MoveOnlyFunction<_Signature>;
#endif
}

namespace oxbox::utilities
{
  using detail::function::MoveOnlyFunction;
}
