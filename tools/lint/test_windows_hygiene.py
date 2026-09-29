"""The Windows macro lint, on fixtures and over the checked-out tree."""
from pathlib import Path

import pytest

import windows_hygiene

NAMES = {"IGNORE": False, "INTERFACE": False, "min": True, "max": True, "_WIN32_WINNT": False}


@pytest.fixture
def tree(tmp_path):
  (tmp_path / "sources" / "pkg" / "unit.test").mkdir(parents=True)
  return tmp_path


def names(tree, text, name="pkg/header.hpp"):
  (tree / "sources" / name).write_text(text)
  return [(finding.line, finding.name) for finding in windows_hygiene.findings(tree, NAMES)]


def test_an_enumerator_named_like_an_object_macro_fails(tree):
  assert names(tree, "enum class Mode { Stop,\n  IGNORE };\n") == [(2, "IGNORE")]


def test_a_local_named_like_an_object_macro_fails(tree):
  assert names(tree, "constexpr auto INTERFACE{ 1 };\n") == [(1, "INTERFACE")]


def test_numeric_limits_max_called_plainly_fails(tree):
  assert names(tree, "auto const top = std::numeric_limits<int>::max();\n") == [(1, "max")]


def test_std_min_called_plainly_fails(tree):
  assert names(tree, "auto const low = std::min (a, b);\n") == [(1, "min")]


def test_a_parenthesised_function_like_name_passes(tree):
  assert names(tree, "auto top = (std::numeric_limits<int>::max)();\nauto low = (std::min)(a, b);\n") == []


def test_a_function_like_name_not_called_passes(tree):
  assert names(tree, "struct Range { int min; int max; };\n") == []


def test_comments_and_literals_pass(tree):
  assert names(tree, '// IGNORE max()\n/* min(a, b) */\nauto s = "IGNORE"; auto r = R"x(max())x";\n') == []


def test_conditional_directives_pass_and_define_bodies_count(tree):
  text = ("#if defined(_WIN32) && !defined(_WIN32_WINNT)\n#define _WIN32_WINNT 0x0A00\n#endif\n"
          "#define PICK(a, b) \\\n  std::max(a, b)\n")
  assert names(tree, text) == [(5, "max")]


def test_digit_separators_open_no_literal(tree):
  text = "auto k = 1'000; auto m = std::max(a, b); auto j = 2'0;\nauto c = u8'a'; auto d = L'('; auto e = 'x';\n"
  assert names(tree, text) == [(1, "max")]


def test_a_defined_name_counts_unless_the_header_tests_it_first(tree):
  text = "#define IGNORE 1\n#ifndef _WIN32_WINNT\n  #define _WIN32_WINNT 0x0A00\n#endif\n"
  assert names(tree, text) == [(1, "IGNORE")]


def test_windows_tagged_headers_are_exempt(tree):
  assert names(tree, "HANDLE IGNORE;\n", "pkg/native.win32.hpp") == []


def test_sources_are_not_headers(tree):
  assert names(tree, "auto top = std::numeric_limits<int>::max();\n", "pkg/unit.test/limits.cpp") == []


def test_the_data_holds_the_names_the_round_was_opened_for():
  macros = windows_hygiene.macros()
  assert {name: macros[name] for name in ("IGNORE", "ERROR", "DELETE", "IN", "OUT", "OPTIONAL", "interface",
                                          "near", "far", "min", "max")} == {
    "IGNORE": False, "ERROR": False, "DELETE": False, "IN": False, "OUT": False, "OPTIONAL": False,
    "interface": False, "near": False, "far": False, "min": True, "max": True}


def test_the_data_leaves_the_standard_library_alone():
  macros = windows_hygiene.macros()
  assert not {"NULL", "EOF", "errno", "SIZE_MAX", "assert", "__cplusplus"} & macros.keys()


def test_a_module_without_its_windows_unit_fails(tree):
  listing = tree / "sources" / "pkg" / "unit.test" / "headers.cpp"
  listing.write_text('#include "pkg/a.hpp"\n')
  assert list(windows_hygiene.unmirrored(tree)) == [Path("sources/pkg/unit.test/headers.cpp")]
  listing.with_name("headers.win32.cpp").write_text('#include <windows.h>\n\n#include "pkg/unit.test/headers.cpp"\n')
  assert list(windows_hygiene.unmirrored(tree)) == []


def test_the_tree_has_no_windows_macro_name_in_a_header():
  assert [f"{finding.path}:{finding.line}: {finding.name}" for finding in windows_hygiene.findings()] == []


def test_every_module_has_its_windows_unit():
  assert list(windows_hygiene.unmirrored()) == []
