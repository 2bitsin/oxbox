#!/usr/bin/env python3
"""Refuse a name in a header that a bare <windows.h> has already defined as a macro."""
from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Iterator, NamedTuple

ROOT = Path(__file__).resolve().parents[2]
DATA = Path(__file__).resolve().with_name("windows-macros.txt")
HEADER_SUFFIXES = {".hpp", ".h", ".inc"}
# Blanked to spaces so line numbers hold; a raw string first, as it may hold the others' delimiters. A quote
# after a word character opens a character literal only after an encoding prefix; otherwise it separates digits.
LITERAL = re.compile(r'R"([^(\s]*)\(.*?\)\1"|"(?:\\.|[^"\\\n])*"'
                     r"|(?:(?<!\w)|(?<=(?<!\w)u8)|(?<=(?<!\w)[uUL]))'(?:\\.|[^'\\\n])*'"
                     r"|//[^\n]*|/\*.*?\*/", re.S)
DIRECTIVE = re.compile(r"^([ \t]*#[ \t]*(\w+)[ \t]*)(\w*)((?:\\\n|[^\n])*)", re.M)
CONDITIONALS = {"if", "ifdef", "ifndef", "elif", "elifdef", "elifndef"}
IDENTIFIER = re.compile(r"\b[A-Za-z_]\w*\b")
CALL = re.compile(r"\s*\(")


class Finding(NamedTuple):
  path: Path
  line: int
  name: str


def macros(data: Path = DATA) -> dict[str, bool]:
  lines = (line.strip() for line in data.read_text().splitlines())
  return {line.removesuffix("()"): line.endswith("()") for line in lines if line and not line.startswith("#")}


def blanked(match: re.Match, group: int = 0) -> str:
  return re.sub(r"[^\n]", " ", match[group])


def tested(text: str) -> set[str]:
  """The names a conditional directive asks about: a #define of one of them is a deliberate default."""
  conditions = (directive[0] for directive in DIRECTIVE.finditer(text) if directive[2] in CONDITIONALS)
  return {name for condition in conditions for name in IDENTIFIER.findall(condition)}


def expanded(text: str) -> str:
  """The text that meets a macro: no literal, no comment, of a directive only a #define's name and body."""
  text     = LITERAL.sub(blanked, text)
  defaults = tested(text)

  def keep(directive: re.Match) -> str:
    if directive[2] != "define":
      return blanked(directive)
    name = blanked(directive, 3) if directive[3] in defaults else directive[3]
    return blanked(directive, 1) + name + directive[4]
  return DIRECTIVE.sub(keep, text)


def windows_tagged(path: Path) -> bool:
  return "win32" in path.name.split(".")[1:-1]


def headers(root: Path) -> Iterator[Path]:
  for path in sorted((root / "sources").rglob("*")):
    if path.suffix in HEADER_SUFFIXES and not windows_tagged(path):
      yield path


def file_findings(root: Path, path: Path, names: dict[str, bool]) -> Iterator[Finding]:
  text = expanded(path.read_text(errors="replace"))
  for match in IDENTIFIER.finditer(text):
    name = match[0]
    if name in names and (not names[name] or CALL.match(text, match.end())):
      yield Finding(path.relative_to(root), text.count("\n", 0, match.start()) + 1, name)


def findings(root: Path = ROOT, names: dict[str, bool] | None = None) -> Iterator[Finding]:
  names = macros() if names is None else names
  for path in headers(root):
    yield from file_findings(root, path, names)


def unmirrored(root: Path = ROOT) -> Iterator[Path]:
  """Every module's unit.test/headers.cpp with no headers.win32.cpp beside it."""
  for listing in sorted((root / "sources").rglob("unit.test/headers.cpp")):
    if not listing.with_name("headers.win32.cpp").exists():
      yield listing.relative_to(root)


def main() -> int:
  found = list(findings())
  for finding in found:
    print(f"{finding.path}:{finding.line}: `{finding.name}` is a macro after <windows.h>; rename it, "
          f"or write `(name)(...)` where it is function-like")
  mirrors = list(unmirrored())
  for mirror in mirrors:
    print(f"{mirror}: has no headers.win32.cpp beside it including it after <windows.h>")
  return int(bool(found or mirrors))


if __name__ == "__main__":
  sys.exit(main())
