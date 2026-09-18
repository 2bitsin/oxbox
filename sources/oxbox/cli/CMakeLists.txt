Init_submodule()
Link_dependencies(
  oxbox-utilities               # errors.hpp, string.hpp, unicode.hpp
)

if(BUILD_TESTING AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"
   AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME cli-short-options-negative
    COMMAND bash "${PROJECT_SOURCE_DIR}/tools/negative-compile.sh"
      --no-msvc --case short-options)
  set_tests_properties(cli-short-options-negative PROPERTIES
    ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}")
endif()
