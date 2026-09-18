"""Requires come from sources/CMakeLists.txt, packaging from buildutil.toml."""

from __future__ import annotations

import os
import json
import re
from pathlib import Path

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


def _package_section() -> dict:
  """[package] from buildutil.toml beside this file; {} means consumer-only."""
  toml = Path(__file__).resolve().parent / "buildutil.toml"
  if not toml.is_file():
    return {}
  import tomllib
  section = tomllib.loads(toml.read_text(encoding="utf-8")).get("package", {})
  return section if section.get("kind") in ("library", "application") else {}


_PKG = _package_section()


REQUIRE_RE = re.compile(
  # yaml-cpp et al. aren't \w-only, so a package name may carry hyphens
  r'^\s*Require\s*\(\s*([\w-]+)\s+VERSION\s+"([^"]+)"(.*?)\)',
  re.MULTILINE | re.DOTALL,
)


_KEYWORDS = {"TEST", "BENCH", "TOOL", "SYSTEM", "CONAN", "COMPONENTS",
             "PLATFORM", "OPTIONS", "PUBLIC"}


def _coerce_option(value: str):
  """Conan option values as their natural types."""
  if value in ("True", "False"):
    return value == "True"
  try:
    return int(value)
  except ValueError:
    return value


def _parse_extra(extra: str) -> dict:
  tokens = extra.replace("\n", " ").split()
  out = {"test": False, "bench": False, "tool": False, "system": False,
         "public": False, "conan": None, "components": [], "platform": [],
         "options": {}}
  sugar = []
  i = 0
  while i < len(tokens):
    # exact-case, as cmake_parse_arguments is: a lowercase `system` is a
    # value (Boost's system component), not the SYSTEM keyword
    t = tokens[i]
    if t == "TEST":
      out["test"] = True
      i += 1
    elif t == "SYSTEM":
      out["system"] = True
      i += 1
    elif t == "BENCH":
      out["bench"] = True
      i += 1
    elif t == "TOOL":
      out["tool"] = True
      i += 1
    elif t == "PUBLIC":
      out["public"] = True
      i += 1
    elif t == "CONAN":
      out["conan"] = tokens[i + 1]
      i += 2
    elif t == "COMPONENTS":
      i += 1
      while i < len(tokens) and tokens[i] not in _KEYWORDS:
        # A leading + or - makes the token option shorthand. Collected and
        # not applied: OPTIONS may be written after COMPONENTS on one call.
        if tokens[i][:1] in ("+", "-"):
          sugar.append(tokens[i])
        else:
          out["components"].append(tokens[i])
        i += 1
    elif t == "PLATFORM":
      i += 1
      while i < len(tokens) and tokens[i] not in _KEYWORDS:
        out["platform"].append(tokens[i])
        i += 1
    elif t == "OPTIONS":
      # A token without '=' is refused loudly: one that silently vanished
      # would leave the package built with the wrong defaults.
      i += 1
      while i < len(tokens) and tokens[i] not in _KEYWORDS:
        key, eq, value = tokens[i].partition("=")
        if not eq or not key:
          raise ValueError(
            f"Require OPTIONS token {tokens[i]!r} is not key=value")
        out["options"][key] = _coerce_option(value)
        i += 1
    else:
      i += 1
  _apply_sugar(out, sugar)
  return out


def _apply_sugar(out: dict, sugar: list) -> None:
  """COMPONENTS tokens written +name / -name: `+asio` is `with_asio=True`."""
  signs = {}
  for token in sugar:
    sign, name = token[0], token[1:]
    if not name:
      raise ValueError(
        f"Require COMPONENTS token {token!r} names no option")
    if signs.setdefault(name, sign) != sign:
      raise ValueError(
        f"Require COMPONENTS has both '+{name}' and '-{name}'")
    for spelling in (f"with_{name}", f"without_{name}"):
      if spelling in out["options"]:
        raise ValueError(
          f"Require COMPONENTS {token!r} and OPTIONS "
          f"{spelling}={out['options'][spelling]!r} both set an option "
          f"for {name!r}")
    out["options"]["with_" + name if sign == "+" else "without_" + name] = True
  if sugar and out["system"]:
    raise ValueError(
      "Require COMPONENTS option shorthand with SYSTEM is meaningless — "
      "a SYSTEM dep is the host's package and conan never builds it")


def _to_conan_version(v: str) -> str:
  v = v.strip()
  if v == "*":
    return "[*]"
  ops = (">=", "<=", ">", "<", "~", "^")
  if not any(v.startswith(op) for op in ops):
    return v
  # Capped at (major+1): non-semver recipe versions like "cci.20210126" sort
  # lexically above real releases and would otherwise win the resolution.
  m = re.match(r"^[><=~^]+\s*(\d+)\.", v)
  if m:
    major = int(m.group(1))
    return f"[{v} <{major + 1}]"
  return f"[{v}]"


def _parse_requires(recipe_folder: Path, target_os: str) -> list[dict]:
  text = (recipe_folder / "sources" / "CMakeLists.txt").read_text()
  entries = []
  for name, version, extra in REQUIRE_RE.findall(text):
    info = _parse_extra(extra)
    # an empty PLATFORM list means every platform
    if info["platform"] and target_os not in info["platform"]:
      continue
    # SYSTEM is the host's, so find_package only and no graph entry: asking
    # conan for a recipe that does not exist fails the install outright.
    if info["system"]:
      continue
    entries.append({
      "conan_name": info["conan"] or name.lower(),
      "version": _to_conan_version(version),
      "test":  info["test"],
      "bench": info["bench"],
      "tool":  info["tool"],
      "public": info["public"],
      "options": info["options"],
    })
  return entries


class ProjectRecipe(ConanFile):
  name = _PKG.get("name", "oxbox")
  license = "MIT"
  author = "Aleksandr Ševčenko"
  url = "https://github.com/2bitsin/oxbox"
  homepage = "https://github.com/2bitsin/oxbox"
  description = ("A C++ library in five modules: command lines, "
                 "serialization, HTTP, platform access and byte utilities.")
  topics = ("cpp", "cli", "serialization", "http")
  settings = "os", "compiler", "build_type", "arch"
  if _PKG:
    package_type = {"library": "library",
                    "application": "application"}[_PKG["kind"]]
    if _PKG["kind"] == "library":
      # the driver passes -o &:shared=True when module_linkage says shared,
      # and conan resolves the library type and its package_ids from it
      options = {"shared": [True, False]}
      default_options = {"shared": False}
    # exports ride with the recipe into the cache, where the cached copy
    # still parses them at graph time; exports_sources only reach a build
    exports = ("buildutil.toml", "sources/CMakeLists.txt")
    # conan hashes what it exports into the recipe revision, so a generated
    # file present on one machine and not another splits a release across two
    # revisions, and consumers resolve only the latest.
    exports_sources = ("CMakeLists.txt", "buildutil.toml", "sources/*",
                       "cmake/*", ".buildutil/*",
                       "!sources/**/cmake_test_discovery_*.json",
                       "!sources/**/__pycache__/**",
                       "!sources/**/*.pyc")

  def set_version(self):
    # The driver computes the version and passes --version; 0.0.0 is a placeholder.
    self.version = self.version or "0.0.0"

  def validate(self):
    # buildutil exports BUILDUTIL=<version> to every child it drives; a bare
    # `conan install .` would resolve a graph outside the driver's contracts
    if not os.environ.get("BUILDUTIL"):
      from conan.errors import ConanInvalidConfiguration
      raise ConanInvalidConfiguration(
        "this project is controlled by buildutil — run `buildutil build` "
        "(conan is orchestrated: profile, CONAN_HOME and the dependency "
        "graph all come from the driver). If you really need direct "
        "conan, set BUILDUTIL=1 in the environment.")

  def layout(self):
    cmake_layout(self)
    profile = _profile_name(self.settings)
    self.folders.build = f"_build/{profile}"
    self.folders.generators = f"_build/{profile}/generators"

  def generate(self):
    CMakeToolchain(self).generate()
    CMakeDeps(self).generate()

  def requirements(self):
    target_os = str(self.settings.os)
    for entry in _parse_requires(Path(self.recipe_folder), target_os):
      if entry["tool"]:
        continue                       # build_requirements() owns these
      ref = f"{entry['conan_name']}/{entry['version']}"
      # OPTIONS ride the requires call rather than default_options, so the
      # option follows the entry's own gating and leaves no stray pattern.
      kwargs = {"options": entry["options"]} if entry["options"] else {}
      # conan does not propagate a static-lib requirement's headers by default
      if entry["public"]:
        kwargs["transitive_headers"] = True
      # conan has no bench_requires, and BENCH has TEST's semantics anyway
      if entry["test"] or entry["bench"]:
        if os.environ.get("OXBOX_SKIP_TEST_DEPS") != "1":
          self.test_requires(ref, **kwargs)   # --no-tests drops these
      else:
        self.requires(ref, **kwargs)

  def build_requirements(self):
    target_os = str(self.settings.os)
    for entry in _parse_requires(Path(self.recipe_folder), target_os):
      if entry["tool"]:
        kwargs = {"options": entry["options"]} if entry["options"] else {}
        self.tool_requires(
          f"{entry['conan_name']}/{entry['version']}", **kwargs)

  # Packaging is export-pkg-based: a cache source-build works only when the
  # package carries its own driver (`publish --bake-buildutil`).

  def build(self):
    if not _PKG:
      return
    import sys
    # folders may be unset on a barely-constructed recipe, and the refusal
    # below must fire rather than an AttributeError
    source = Path(getattr(self, "source_folder", None)
                  or getattr(self, "recipe_folder", None) or ".")
    vendored = source / ".buildutil"
    if (vendored / "buildutil" / "__main__.py").is_file():
      # conan has already resolved the Require()s and generated the toolchain,
      # so `cache-build` needs no venv, no nested conan and no network
      shared = self.options.get_safe("shared")
      toolchain = Path(self.generators_folder) / "conan_toolchain.cmake"
      previous = os.environ.get("PYTHONPATH")
      os.environ["PYTHONPATH"] = (
        f"{vendored}{os.pathsep}{previous}" if previous else str(vendored))
      try:
        self.run(
          f'"{sys.executable}" -m buildutil cache-build'
          f' --build-dir "{self.build_folder}"'
          f' --toolchain "{toolchain}"'
          f' --build-type {self.settings.build_type}'
          f' --linkage {"shared" if shared else "static"}',
          cwd=str(source))
      finally:
        if previous is None:
          os.environ.pop("PYTHONPATH", None)
        else:
          os.environ["PYTHONPATH"] = previous
      return
    from conan.errors import ConanException
    # settings is still the class-level tuple until conan populates it
    version = getattr(self, "version", None)
    ref = f"{self.name}/{version}" if version else self.name
    settings = getattr(self, "settings", None)
    def _setting(name):
      return settings.get_safe(name) if hasattr(settings, "get_safe") else "?"
    profile = (f"build_type={_setting('build_type')}, "
               f"compiler={_setting('compiler')}-{_setting('compiler.version')}, "
               f"cppstd={_setting('compiler.cppstd')}")
    raise ConanException(
      f"{self.name}: building this package from source inside the conan "
      "cache is not supported — it was published WITHOUT its build "
      "driver baked in (`buildutil publish --bake-buildutil` changes "
      "that), so binaries come from the project remote, which publish "
      "keeps populated. Fetch a prebuilt binary, or clone the project "
      "and run `buildutil publish` for your profile.\n"
      f"You are here because no published binary matched your profile: "
      f"{profile}.\n"
      "SEE WHAT IS ACTUALLY PUBLISHED FIRST — it settles this in one "
      f"command:\n    conan list \"{ref}:*\" -r <remote>\n"
      "If the published list simply has no entry for your build_type "
      "(publishing Release but not Debug, or the reverse, is the common "
      "case), the fix is on the PUBLISHER: run `buildutil publish` for "
      "the missing profile. Nothing is wrong on your side.\n"
      "Only if your build_type IS published does the package_id-drift "
      "explanation apply: a version-RANGED dependency of this "
      "package resolved differently in your cache than at publish time. "
      "Compare `conan graph info` resolutions against the published "
      "package's requires and align them (update/pin the drifting dep) "
      "instead of building from source.")

  def package(self):
    if not _PKG:
      return
    # layout() points build_folder at the tree buildutil just built
    self.run(f'cmake --install "{self.build_folder}" '
             f'--prefix "{self.package_folder}"')

  def package_info(self):
    if not _PKG:
      return
    root = Path(self.package_folder)
    if _PKG["kind"] == "application":
      bindirs = sorted({
        str(p.parent.relative_to(root)) for p in root.rglob("*")
        if p.is_file() and os.access(p, os.X_OK)})
      self.cpp_info.bindirs = bindirs or ["."]
      self.cpp_info.libdirs = []
      self.cpp_info.includedirs = []
      return
    libs, libdirs = set(), set()
    for p in root.rglob("*"):
      if not p.is_file():
        continue
      if p.suffix in (".a", ".lib") or p.suffix in (".so", ".dylib") \
         or ".so." in p.name:
        stem = p.name.split(".")[0]
        libs.add(stem[3:] if stem.startswith("lib") else stem)
        libdirs.add(str(p.parent.relative_to(root)))
    incdirs = ["include"] if (root / "include").is_dir() else []
    # conan keeps <pkg>::<pkg> as the aggregate beside the per-module
    # targets, so the coarse spelling never breaks.
    manifest = root / "share" / "buildutil" / "buildutil-components.json"
    comps = []
    if manifest.is_file():
      try:
        comps = json.loads(manifest.read_text()).get("components", [])
      except ValueError:
        comps = []
    # one module is not worth componentising: the aggregate IS the module
    if len(comps) > 1:
      def _cname(path):
        # nested modules keep their depth: sources/<pkg>/net/http -> net-http
        parts = [x for x in path.split("/") if x]
        if parts and parts[0] == self.name:
          parts = parts[1:]
        return "-".join(parts) or self.name
      by_path = {c["path"]: _cname(c["path"]) for c in comps}
      for c in comps:
        name = by_path[c["path"]]
        comp = self.cpp_info.components[name]
        comp.libs = [c["lib"]] if c["lib"] in libs else []
        comp.libdirs = sorted(libdirs) or ["lib"]
        comp.includedirs = incdirs
        reqs = [by_path[n] for n in c.get("needs", []) if n in by_path]
        for ext in c.get("external", []):
          # a conan target is pkg::comp; anything else is a system lib
          if "::" in ext:
            reqs.append(ext)
          else:
            comp.system_libs.append(ext)
        comp.requires = reqs
      return
    self.cpp_info.libs = sorted(libs)
    self.cpp_info.libdirs = sorted(libdirs) or ["lib"]
    self.cpp_info.includedirs = incdirs


def _module_linkage() -> str:
  """module_linkage, resolved as the driver resolves it: env, ini, then static."""
  env = os.environ.get("BUILDUTIL_OPT_MODULE_LINKAGE", "")
  if env in ("static", "shared"):
    return env
  ini = Path(__file__).resolve().parent / "_bdudata" / "config.ini"
  if ini.is_file():
    section = ""
    for raw in ini.read_text().splitlines():
      line = raw.split("#", 1)[0].strip()
      if not line:
        continue
      if line.startswith("[") and line.endswith("]"):
        section = line[1:-1].strip().lower()
      elif section == "options":
        key, _, value = line.partition("=")
        if key.strip() == "module_linkage" and value.strip() in (
            "static", "shared"):
          return value.strip()
  return "static"


def _profile_name(settings) -> str:
  parts = [str(settings.arch), str(settings.os), str(settings.compiler)]
  linkage = _module_linkage()
  if linkage != "static":
    parts.append(linkage)
  parts.append(str(settings.build_type))
  return "-".join(parts).lower()
