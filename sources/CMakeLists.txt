Require(GTest VERSION ">=1.17.0" TEST CONAN gtest COMPONENTS gtest gtest_main gmock gmock_main)
Require(nlohmann_json VERSION "3.11.3" CONAN nlohmann_json PUBLIC)
Require(yaml-cpp VERSION "0.9.0" CONAN yaml-cpp PUBLIC)
Require(pugixml VERSION "1.16" CONAN pugixml PUBLIC)
# header_only is one settings-independent package for every profile
Require(Boost VERSION "1.91.0" CONAN boost OPTIONS header_only=True PUBLIC
        PLATFORM Linux Macos Windows)
Require(OpenSSL VERSION "3.6.3" CONAN openssl PUBLIC
        PLATFORM Linux Macos Windows)

# One ctest entry per compile, so a parallel gate spreads the units over the cores.
function(Add_negative_compile_tests case)
  if(NOT BUILD_TESTING OR NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
    return()
  endif()
  set(script "${PROJECT_SOURCE_DIR}/tools/negative-compile.sh")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${script}")
  execute_process(
    COMMAND bash "${script}" --list --case ${case}
    OUTPUT_VARIABLE units
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  string(REPLACE "\n" ";" units "${units}")
  foreach(unit IN LISTS units)
    foreach(compiler IN ITEMS gcc clang)
      add_test(NAME negative-compile.${compiler}.${unit}
        COMMAND bash "${script}" --compiler ${compiler} --unit ${unit})
      set_tests_properties(negative-compile.${compiler}.${unit} PROPERTIES
        ENVIRONMENT      "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}"
        SKIP_RETURN_CODE 77)
    endforeach()
  endforeach()
endfunction()

Scan_subdirectories()
