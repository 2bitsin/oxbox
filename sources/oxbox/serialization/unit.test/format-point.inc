  struct Point
  {
    friend constexpr auto reflect_scheme(Point*);
    std::int64_t x{};
    std::int64_t y{};
    std::string  label;
    auto operator==(Point const&) const -> bool = default;
  };

  constexpr auto reflect_scheme(Point*)
  {
    using T = Point;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"x",     &T::x>,
      ::reflect::member_scheme<"y",     &T::y>,
      ::reflect::member_scheme<"label", &T::label>>{ };
  }

