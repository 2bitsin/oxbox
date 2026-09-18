Init_submodule()
# Diagnostics spell a path with PathToString: std::filesystem::path::string()
# throws on Windows for a name the narrow encoding cannot hold.
Link_dependencies(
  oxbox-utilities   # span, short-types, path
)
