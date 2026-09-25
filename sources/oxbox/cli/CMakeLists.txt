Init_submodule()
Link_dependencies(
  oxbox-utilities               # errors.hpp, string.hpp, unicode.hpp
)
Add_negative_compile_tests(short-options)
Add_negative_compile_tests(one-name-per-declaration)
