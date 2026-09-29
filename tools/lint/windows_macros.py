#!/usr/bin/env python3
"""Regenerate windows-macros.txt with cl /PD in the image OXBOX_MSVC_IMAGE names."""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

DATA = Path(__file__).resolve().with_name("windows-macros.txt")
STANDARD_HEADERS = (
  "algorithm any array atomic barrier bit bitset charconv chrono codecvt compare complex concepts"
  " condition_variable coroutine deque exception execution expected filesystem format forward_list fstream"
  " functional future generator initializer_list iomanip ios iosfwd iostream istream iterator latch limits list"
  " locale map mdspan memory memory_resource mutex new numbers numeric optional ostream print queue random ranges"
  " ratio regex scoped_allocator semaphore set shared_mutex source_location span spanstream sstream stack"
  " stacktrace stdexcept stop_token streambuf string string_view syncstream system_error thread tuple type_traits"
  " typeindex typeinfo unordered_map unordered_set utility valarray variant vector version cassert cctype cerrno"
  " cfenv cfloat cinttypes climits clocale cmath csetjmp csignal cstdarg cstddef cstdint cstdio cstdlib cstring"
  " ctime cuchar cwchar cwctype").split()
DEFINE = re.compile(r"#define (\w+)(\()?")
CL = ("/opt/msvc/bin/x64/cl", "/EP", "/PD", "/Zc:preprocessor", "/std:c++latest", "/EHsc")


def definitions(text: str) -> dict[str, bool]:
  return {match[1]: bool(match[2]) for match in map(DEFINE.match, text.splitlines()) if match}


def spelled(name: str, function_like: bool) -> str:
  return name + "()" if function_like else name


def container(image: str, work: Path, name: str, *command: str) -> subprocess.CompletedProcess:
  label = f"mdx-oxbox-windows-macros-{os.getpid()}-{name}"
  argv  = ["docker", "run", "--rm", "--name", label, "-v", f"{work}:/w:ro", "-w", "/w", image, *command]
  run = subprocess.run(argv, capture_output=True, text=True, errors="replace")
  sys.stderr.write(run.stderr)
  run.check_returncode()
  return run


def preprocessed(image: str, work: Path, name: str, includes: list[str]) -> subprocess.CompletedProcess:
  (work / f"{name}.cpp").write_text("".join(f"#include <{header}>\n" for header in includes))
  return container(image, work, name, *CL, f"{name}.cpp")


def digest(image: str) -> str:
  named = subprocess.run(["docker", "image", "inspect", "--format", "{{index .RepoDigests 0}}", image],
                         check=True, capture_output=True, text=True).stdout.strip()
  return "image " + named.rpartition("@")[2]


def render(image: str, work: Path) -> str:
  windows  = preprocessed(image, work, "windows", ["windows.h"])
  standard = preprocessed(image, work, "standard", STANDARD_HEADERS)
  added    = definitions(windows.stdout)
  for name in definitions(standard.stdout):
    added.pop(name, None)
  compiler = windows.stderr.splitlines()[0]
  sdk      = container(image, work, "sdk", "ls", "/opt/msvc/kits/10/Include").stdout.split()[-1]
  header   = ("# #define names a bare <windows.h> adds to the compiler's and the C++ standard library's;"
              " `()` is function-like.\n"
              f"# {compiler}, Windows SDK {sdk}, {digest(image)}; tools/lint/windows_macros.py.\n")
  return header + "".join(spelled(name, added[name]) + "\n" for name in sorted(added))


def main() -> int:
  image = os.environ.get("OXBOX_MSVC_IMAGE")
  if not image:
    print("windows_macros.py: set OXBOX_MSVC_IMAGE to the msvc-wine image", file=sys.stderr)
    return 2
  with tempfile.TemporaryDirectory(prefix="mdx-oxbox-windows-macros-") as scratch:
    DATA.write_text(render(image, Path(scratch)))
  return 0


if __name__ == "__main__":
  sys.exit(main())
