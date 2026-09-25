#!/usr/bin/env bash
#
# negative-compile -- compile every fixture that must be refused and check the
# diagnostic carries the guard's own sentence, which gtest cannot do.
#
#   tools/negative-compile.sh                  # every case, gcc + clang + cl
#   tools/negative-compile.sh --no-msvc        # skip the container leg
#   tools/negative-compile.sh --case hex       # only rows whose name matches
#   tools/negative-compile.sh --fixture FILE --expect TEXT
#   tools/negative-compile.sh --list           # the units, one compile each
#   tools/negative-compile.sh --compiler gcc --unit NAME   # one compile
#
# The cl leg reads OXBOX_MSVC_IMAGE. A build tree must exist for the one
# generated header parse.hpp includes, <_buildutil/reflect.hpp>. Exit 77 is
# a run where every selected leg was skipped and nothing was judged.

set -euo pipefail

readonly HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO="$(cd -- "$HERE/.." && pwd)"


# A row: name, the fixture that must not compile, a fragment of the guard's
# own message (which keeps a broken include path from reading as a pass), a
# name the message must lead the reader to or empty, and a control that must
# compile. The separator is the ASCII unit separator and not a tab, which is
# an IFS whitespace character `read` collapses runs of.
readonly FS=$'\x1f'
CASES=(
  "number-text.parse-numbers.floating"$FS"$HERE/negative-compile/parse-numbers.floating.cpp"$FS"constraints not satisfied"$FS"is_integral_v"$FS"$HERE/negative-compile/number-text.accepts.cpp"
  "number-text.parse-number-after.floating"$FS"$HERE/negative-compile/parse-number-after.floating.cpp"$FS"constraints not satisfied"$FS"is_integral_v"$FS"$HERE/negative-compile/number-text.accepts.cpp"
  "growing-writer.non-integral"$FS"$HERE/negative-compile/growing-writer.non-integral.cpp"$FS"GrowingWriter::Put requires an integral or enum type"$FS""$FS"$HERE/negative-compile/growing-writer.accepts.cpp"
  "one-name-per-declaration"$FS"$HERE/negative-compile/one-name-per-declaration.collides.cpp"$FS"answer to the same command-line name"$FS"content-type"$FS"$HERE/negative-compile/one-name-per-declaration.distinct.cpp"
  "exception.missing-argument"$FS"$HERE/negative-compile/exception.missing-argument.cpp"$FS"__invalid_arg_id_in_format_string"$FS"FORMAT_STRING"$FS"$HERE/negative-compile/exception.accepts.cpp"
  "exception.wrong-type"$FS"$HERE/negative-compile/exception.wrong-type.cpp"$FS"__failed_to_parse_format_spec"$FS"FORMAT_STRING"$FS"$HERE/negative-compile/exception.accepts.cpp"
  "exception.named-in-catch"$FS"$HERE/negative-compile/exception.named-in-catch.cpp"$FS"__invalid_arg_id_in_format_string"$FS"FORMAT_STRING"$FS"$HERE/negative-compile/exception.accepts.cpp"
  "exception.final-what"$FS"$HERE/negative-compile/exception.final-what.cpp"$FS"overrid"$FS"OwnedText"$FS"$HERE/negative-compile/exception.overrides-what.cpp"
  "hex-literal.binary-form"$FS"$HERE/negative-compile/hex-literal.binary-form.cpp"$FS"a leading 0 that is not 0x is not hex"$FS""$FS"$HERE/negative-compile/hex-literal.accepts.cpp"
  "hex-literal.stray-character"$FS"$HERE/negative-compile/hex-literal.stray-character.cpp"$FS"a stray character, or an odd number of digits"$FS""$FS"$HERE/negative-compile/hex-literal.accepts.cpp"
  "short-options.duplicate.verbose"$FS"$HERE/negative-compile/short-options.duplicate.cpp"$FS"two members claim the same short spelling"$FS"verbose"$FS"$HERE/negative-compile/short-options.distinct.cpp"
  "short-options.duplicate.output"$FS"$HERE/negative-compile/short-options.duplicate.cpp"$FS"two members claim the same short spelling"$FS"output"$FS"$HERE/negative-compile/short-options.distinct.cpp"
  "short-options.bad-tag.verbose"$FS"$HERE/negative-compile/short-options.bad-tag.cpp"$FS"a short option tag must be a dash and exactly one character"$FS"verbose"$FS"$HERE/negative-compile/short-options.distinct.cpp"
  "short-options.bare-dash.verbose"$FS"$HERE/negative-compile/short-options.bare-dash.cpp"$FS"a short option tag must be a dash and exactly one character"$FS"verbose"$FS"$HERE/negative-compile/short-options.distinct.cpp"
)

WITH_MSVC=1
ONLY=""
ONLY_UNIT=""
LEGS=( gcc clang cl )
LIST=0
AD_HOC_FIXTURE=""
AD_HOC_EXPECT=""
AD_HOC_SPELLING=""

while [ $# -gt 0 ]; do
  case "$1" in
    --no-msvc)  WITH_MSVC=0; shift ;;
    --case)     ONLY="$2"; shift 2 ;;
    --unit)     ONLY_UNIT="$2"; shift 2 ;;
    --compiler) LEGS=( "$2" ); shift 2 ;;
    --list)     LIST=1; shift ;;
    --fixture)  AD_HOC_FIXTURE="$(cd -- "$(dirname -- "$2")" && pwd)/$(basename -- "$2")"; shift 2 ;;
    --expect)   AD_HOC_EXPECT="$2"; shift 2 ;;
    --spelling) AD_HOC_SPELLING="$2"; shift 2 ;;
    -h|--help)  sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//;$d'; exit 0 ;;
    *)          echo "negative-compile: unknown argument '$1'" >&2; exit 2 ;;
  esac
done

# A refusal nobody named is just a compile that failed.
if [ -n "$AD_HOC_FIXTURE" ]; then
  if [ -z "$AD_HOC_EXPECT" ]; then
    echo "negative-compile: --fixture needs --expect TEXT (the sentence the refusal must carry)" >&2
    exit 2
  fi
  CASES=( "ad-hoc"$FS"$AD_HOC_FIXTURE"$FS"$AD_HOC_EXPECT"$FS"$AD_HOC_SPELLING"$FS"" )
  ONLY=""
fi

if [ -n "$ONLY" ]; then
  declare -a SELECTED=( )
  for row in "${CASES[@]}"; do
    case "${row%%"$FS"*}" in *"$ONLY"*) SELECTED+=( "$row" ) ;; esac
  done
  if [ "${#SELECTED[@]}" -eq 0 ]; then
    echo "negative-compile: no case matches '$ONLY'" >&2
    exit 2
  fi
  CASES=( "${SELECTED[@]}" )
fi

# A unit is one compile: each row's refusal, and each distinct control once.
# Its fields: kind (refuse or accept), name, source, refusal, spelling.
declare -a UNITS=( )
declare -A CONTROLS=( )
for row in "${CASES[@]}"; do
  IFS="$FS" read -r name fixture refusal spelling control <<< "$row"
  UNITS+=( "refuse$FS$name$FS$fixture$FS$refusal$FS$spelling" )
  if [ -n "$control" ] && [ -z "${CONTROLS[$control]:-}" ]; then
    CONTROLS[$control]=1
    UNITS+=( "accept$FS$(basename -- "$control" .cpp)$FS$control$FS$FS" )
  fi
done

if [ -n "$ONLY_UNIT" ]; then
  declare -a CHOSEN=( )
  for unit in "${UNITS[@]}"; do
    IFS="$FS" read -r _ name _ <<< "$unit"
    if [ "$name" = "$ONLY_UNIT" ]; then CHOSEN+=( "$unit" ); fi
  done
  if [ "${#CHOSEN[@]}" -eq 0 ]; then
    echo "negative-compile: no unit is named '$ONLY_UNIT' (see --list)" >&2
    exit 2
  fi
  UNITS=( "${CHOSEN[@]}" )
fi

if [ "$LIST" -eq 1 ]; then
  for unit in "${UNITS[@]}"; do
    IFS="$FS" read -r _ name _ <<< "$unit"
    printf '%s\n' "$name"
  done
  exit 0
fi

for leg in "${LEGS[@]}"; do
  case "$leg" in
    gcc|clang|cl) ;;
    *) echo "negative-compile: unknown compiler '$leg' (gcc, clang or cl)" >&2; exit 2 ;;
  esac
done

FAILURES=0
PASSES=0
SKIPS=0
declare -a SUMMARY=( )

Note()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
Pass()  { printf '   \033[32mok\033[0m      %s\n' "$*"; SUMMARY+=( "ok      $*" ); PASSES=$(( PASSES + 1 )); }
Fail()  { printf '   \033[31mFAILED\033[0m  %s\n' "$*"; SUMMARY+=( "FAILED  $*" ); FAILURES=$(( FAILURES + 1 )); }
Skip()  { printf '   \033[33mSKIPPED\033[0m %s\n' "$*"; SUMMARY+=( "SKIPPED $*" ); SKIPS=$(( SKIPS + 1 )); }

# <_buildutil/reflect.hpp> is the same file in every build directory.
REFLECT_ROOT=""
for candidate in "$REPO"/_build/*/generated; do
  if [ -f "$candidate/_buildutil/reflect.hpp" ]; then REFLECT_ROOT="$candidate"; break; fi
done
if [ -z "$REFLECT_ROOT" ]; then
  cat >&2 <<'MISSING'
negative-compile: no generated reflect header under _build/*/generated.

  parse.hpp includes <_buildutil/reflect.hpp>, which buildutil renders at
  build time; nothing else here needs a build tree. Run

      ./buildutil build \
        --skip-dependency-upload-so-everyone-rebuilds-from-source

  once and try again. (Refusing rather than guessing: a fixture that fails
  to compile because a header was missing looks exactly like a fixture the
  guard refused, and that confusion is the whole thing this check is
  against.)
MISSING
  exit 2
fi

readonly WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

printf 'negative-compile: %s\n' "$REPO"
printf '  reflect header from:       %s\n' "${REFLECT_ROOT#$REPO/}"
printf '  units (%d) on %s:\n' "${#UNITS[@]}" "${LEGS[*]}"
for unit in "${UNITS[@]}"; do
  IFS="$FS" read -r kind name source refusal spelling <<< "$unit"
  printf '    %-40s %s %s\n' "$name" "$kind" "${source#$REPO/}"
  if [ "$kind" = refuse ]; then
    printf '    %-40s expect "%s"%s\n' "" "$refusal" \
           "$([ -n "$spelling" ] && printf ' naming "%s"' "$spelling")"
  fi
done

# One standard for gcc, clang and cl.

Judge_refusal()
{
  local label="$1" status="$2" log="$3" refusal="$4" spelling="$5"

  if [ "$status" -eq 0 ]; then
    Fail "$label: the fixture COMPILED CLEANLY -- the guard did not fire"
    return
  fi
  if ! grep -qF -- "$refusal" "$log"; then
    Fail "$label: compile failed, but not with the collision refusal"
    sed -n '1,12p' "$log" | sed 's/^/          /'
    return
  fi
  if [ -z "$spelling" ]; then
    Pass "$label: refused, by its own sentence"
    return
  fi
  if ! grep -qF -- "$spelling" "$log"; then
    # cl prints a char-array template argument as decimal character codes, so
    # the name is there complete and in order, simply not as letters.
    local codes; codes="$(printf '%s' "$spelling" | od -An -tu1 -v | tr -s ' ' | sed 's/^ //;s/ /,/g')"
    if grep -qF -- "$codes" "$log"; then
      Pass "$label: refused, naming '$spelling' as decimal character codes"
      printf '          the codes are %s -- documented cl rendering\n' "$codes"
      return
    fi
    Fail "$label: the refusal fired but carries '$spelling' in no form at all"
    sed -n '1,12p' "$log" | sed 's/^/          /'
    return
  fi
  Pass "$label: refused, naming '$spelling'"
}

Judge_control()
{
  local label="$1" status="$2" log="$3"

  if [ "$status" -ne 0 ]; then
    Fail "$label: the control fixture DID NOT COMPILE -- the check cannot tell a guard from a broken include path"
    sed -n '1,12p' "$log" | sed 's/^/          /'
    return
  fi
  Pass "$label: the control compiles, so the guard is not simply always on"
}

Judge_unit()
{
  local label="$1" kind="$2" status="$3" log="$4" refusal="$5" spelling="$6"

  case "$kind" in
    refuse) Judge_refusal "$label" "$status" "$log" "$refusal" "$spelling" ;;
    accept) Judge_control "$label" "$status" "$log" ;;
    *)      Fail "$label: unit kind '$kind' is neither refuse nor accept" ;;
  esac
}

# Each unit's source and log are appended to the compile command it is given.
Run_units()
{
  local label="$1"; shift

  local unit kind name source refusal spelling
  for unit in "${UNITS[@]}"; do
    IFS="$FS" read -r kind name source refusal spelling <<< "$unit"
    local log="$WORK/$label.$name.log"
    set +e
    "$@" "$source" "$log"
    local status=$?
    set -e
    Judge_unit "$label/$name" "$kind" "$status" "$log" "$refusal" "$spelling"
  done
}

# The flags mirror what buildutil hands these two; a judged fixture never links.
Host_compile()
{
  local compiler="$1" budget="$2" source="$3" log="$4"

  "$compiler" -std=c++26 "$budget" -I"$REFLECT_ROOT" -I"$REPO/sources" \
    -c -o "$WORK/out.o" "$source" > "$log" 2>&1
}

Host_leg()
{
  local label="$1" compiler="$2" budget="$3"

  Note "$label"
  if ! command -v "$compiler" > /dev/null 2>&1; then
    Skip "$label: no '$compiler' on PATH"
    return
  fi
  Run_units "$label" Host_compile "$compiler" "$budget"
}

# cl's banner and the file name it echoes are filtered out of the diagnostic.
# The repo goes in read-only, so a container leg touches no working tree.
Msvc_compile()
{
  local source="$1" log="$2"

  docker run --rm -v "$REPO:/src:ro" -w /tmp "$MSVC_IMAGE" bash -lc \
    "/opt/msvc/bin/x64/cl /c /EHsc /permissive- /W4 /std:c++latest \
     /I/src/${REFLECT_ROOT#$REPO/} /I/src/sources /Fo:/tmp/negative-compile.obj \
     /src/${source#$REPO/}" \
    > "$log.raw" 2>&1
  local status=$?

  grep -vE '^Microsoft \(R\)|^Copyright \(C\)|/std:c\+\+latest is provided|^[A-Za-z0-9_.-]+\.cpp$' \
    "$log.raw" > "$log" || true
  return "$status"
}

Msvc_leg()
{
  local label="cl"

  Note "cl 19.51 (msvc-wine, the guard's #else branch)"
  if [ "$WITH_MSVC" -eq 0 ]; then
    Skip "$label: --no-msvc was given, so the guard's #else branch went UNCOVERED"
    return
  fi
  local MSVC_IMAGE="${OXBOX_MSVC_IMAGE:?set OXBOX_MSVC_IMAGE to the msvc-wine image}"
  if ! command -v docker > /dev/null 2>&1; then
    Skip "$label: no docker on this machine, so the guard's #else branch went UNCOVERED"
    return
  fi
  if ! docker image inspect "$MSVC_IMAGE" > /dev/null 2>&1; then
    Skip "$label: image $MSVC_IMAGE is not present (docker pull it), so the guard's #else branch went UNCOVERED"
    return
  fi

  # Run_units reads UNITS, and this local shadows it with what the mount reaches.
  local -a selected=( "${UNITS[@]}" )
  local -a UNITS=( )
  local unit name source
  for unit in "${selected[@]}"; do
    IFS="$FS" read -r _ name source _ <<< "$unit"
    case "$source" in
      "$REPO"/*) UNITS+=( "$unit" ) ;;
      *) Skip "$label/$name: source is outside $REPO, which the container mount cannot reach" ;;
    esac
  done
  Run_units "$label" Msvc_compile
}

for leg in "${LEGS[@]}"; do
  case "$leg" in
    gcc)   Host_leg gcc   g++     -fconstexpr-ops-limit=100000000 ;;
    clang) Host_leg clang clang++ -fconstexpr-steps=100000000 ;;
    cl)    Msvc_leg ;;
  esac
done

Note "summary"
printf '   %s\n' "${SUMMARY[@]}"

if [ "$FAILURES" -ne 0 ]; then
  printf '\nnegative-compile: %d failed, %d skipped.\n' "$FAILURES" "$SKIPS"
  exit 1
fi
if [ "$PASSES" -eq 0 ]; then
  printf '\nnegative-compile: nothing was judged; %d skipped.\n' "$SKIPS"
  exit 77
fi
if [ "$SKIPS" -ne 0 ]; then
  printf '\nnegative-compile: all run legs passed, but %d were SKIPPED and their branch is uncovered.\n' "$SKIPS"
  exit 0
fi
printf '\nnegative-compile: every leg passed.\n'
