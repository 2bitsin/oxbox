Init_submodule()
Link_dependencies(
  oxbox-utilities               # FromU8String / path helpers (unicode.hpp)
  nlohmann_json::nlohmann_json  # JSON backend
  yaml-cpp::yaml-cpp            # YAML backend
  pugixml::pugixml              # XML backend
)
