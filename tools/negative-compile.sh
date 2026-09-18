#!/usr/bin/env bash
#
# negative-compile -- compile every fixture that must be refused and check the
# diagnostic carries the guard's own sentence, which gtest cannot do.
#
#   tools/negative-compile.sh                  # every case, gcc + clang + cl
#   tools/negative-compile.sh --no-msvc        # skip the container leg
#   tools/negative-compile.sh --case hex       # only rows whose name matches
#   tools/negative-compile.sh --fixture FILE --expect TEXT
#
# The cl leg reads OXBOX_MSVC_IMAGE. A build tree must exist for the one
# generated header parse.hpp includes, <_buildutil/reflect.hpp>.

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
  "one-name-per-declaration"$FS"$HERE/negative-compile/one-name-per-declaration.collides.cpp"$FS"answer to the same command-line name"$FS"content-type"$FS"$HERE/negative-compile/one-name-per-declaration.distinct.cpp"
  "hex-literal.binary-form"$FS"$HERE/negative-compile/hex-literal.binary-form.cpp"$FS"a leading 0 that is not 0x is not hex"$FS""$FS"$HERE/negative-compile/hex-literal.accepts.cpp"
  "hex-literal.stray-character"$FS"$HERE/negative-compile/hex-literal.stray-character.cpp"$FS"a stray character, or an odd number of digits"$FS""$FS"$HERE/negative-compile/hex-literal.accepts.cpp"
  "short-options.duplicate.verbose"$FS"$HERE/negative-compile/short-options.duplicate.cpp"$FS"two members claim the same short spelling"$FS"verbose"$FS"$HERE/negative-compile/short-options.distinct.cpp"
  "short-options.duplicate.output"$FS"$HERE/negative-compile/short-options.duplicate.cpp"$FS"two members claim the same short spelling"$FS"output"$FS"$HERE/negative-compile/short-options.distinct.cpp"
  "short-options.bad-tag.verbose"$FS"$HERE/negative-compile/short-options.bad-tag.cpp"$FS"a short option tag must be a dash and exactly one character"$FS"verbose"$FS"$HERE/negative-compile/short-options.distinct.cpp"
  "short-options.bare-dash.verbose"$FS"$HERE/negative-compile/short-options.bare-dash.cpp"$FS"a short option tag must be a dash and exactly one character"$FS"verbose"$FS"$HERE/negative-compile/short-options.distinct.cpp"
)

WITH_MSVC=1
ONLY=""
AD_HOC_FIXTURE=""
AD_HOC_EXPECT=""
AD_HOC_SPELLING=""

while [ $# -gt 0 ]; do
  case "$1" in
    --no-msvc)  WITH_MSVC=0; shift ;;
    --case)     ONLY="$2"; shift 2 ;;
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

FAILURES=0
SKIPS=0
declare -a SUMMARY=( )

Note()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
Pass()  { printf '   \033[32mok\033[0m      %s\n' "$*"; SUMMARY+=( "ok      $*" ); }
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
printf '  cases (%d):\n' "${#CASES[@]}"
for row in "${CASES[@]}"; do
  IFS="$FS" read -r name fixture refusal spelling control <<< "$row"
  printf '    %-28s refuse %s\n' "$name" "${fixture#$REPO/}"
  printf '    %-28s expect "%s"%s\n' "" "$refusal" \
         "$([ -n "$spelling" ] && printf ' naming "%s"' "$spelling")"
  [ -n "$control" ] && printf '    %-28s accept %s\n' "" "${control#$REPO/}"
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

# The flags mirror what buildutil hands these two; a judged fixture never links.

Host_leg()
{
  local label="$1" compiler="$2" budget="$3"

  Note "$label"
  if ! command -v "$compiler" > /dev/null 2>&1; then
    Skip "$label: no '$compiler' on PATH"
    return
  fi

  local flags=( -std=c++26 "$budget" -I"$REFLECT_ROOT" -I"$REPO/sources" -c -o "$WORK/out.o" )

  local row name fixture refusal spelling control
  for row in "${CASES[@]}"; do
    IFS="$FS" read -r name fixture refusal spelling control <<< "$row"
    local case_label="$label/$name"

    set +e
    "$compiler" "${flags[@]}" "$fixture" > "$WORK/$label.$name.refusal.log" 2>&1
    local refused=$?
    set -e
    Judge_refusal "$case_label" "$refused" "$WORK/$label.$name.refusal.log" \
                  "$refusal" "$spelling"

    [ -n "$control" ] || continue
    set +e
    "$compiler" "${flags[@]}" "$control" > "$WORK/$label.$name.control.log" 2>&1
    local accepted=$?
    set -e
    Judge_control "$case_label" "$accepted" "$WORK/$label.$name.control.log"
  done
}

# cl's banner and the file name it echoes are filtered out of the diagnostic.
Msvc_compile()
{
  local source="$1" log="$2" reflect="$3"

  docker run --rm -v "$REPO:/src:ro" -w /tmp "$MSVC_IMAGE" bash -lc \
    "/opt/msvc/bin/x64/cl /c /EHsc /permissive- /W4 /std:c++latest \
     /I$reflect /I/src/sources /Fo:/tmp/negative-compile.obj $source" \
    > "$log.raw" 2>&1
  local status=$?

  grep -vE '^Microsoft \(R\)|^Copyright \(C\)|/std:c\+\+latest is provided|^[A-Za-z0-9_.-]+\.cpp$' \
    "$log.raw" > "$log" || true
  return "$status"
}

# The repo goes in read-only, so a container leg touches no working tree.

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

  local reflect_in_container="/src/${REFLECT_ROOT#$REPO/}"

  local row name fixture refusal spelling control
  for row in "${CASES[@]}"; do
    IFS="$FS" read -r name fixture refusal spelling control <<< "$row"
    local case_label="$label/$name"

    # Everything the container sees comes in under /src.
    case "$fixture" in
      "$REPO"/*) ;;
      *) Skip "$case_label: fixture is outside $REPO, which the container mount cannot reach"
         continue ;;
    esac

    set +e
    Msvc_compile "/src/${fixture#$REPO/}" "$WORK/$label.$name.refusal.log" \
                 "$reflect_in_container"
    local refused=$?
    set -e
    Judge_refusal "$case_label" "$refused" "$WORK/$label.$name.refusal.log" \
                  "$refusal" "$spelling"

    [ -n "$control" ] || continue
    set +e
    Msvc_compile "/src/${control#$REPO/}" "$WORK/$label.$name.control.log" \
                 "$reflect_in_container"
    local accepted=$?
    set -e
    Judge_control "$case_label" "$accepted" "$WORK/$label.$name.control.log"
  done
}

Host_leg gcc   g++     -fconstexpr-ops-limit=100000000
Host_leg clang clang++ -fconstexpr-steps=100000000
Msvc_leg

Note "summary"
printf '   %s\n' "${SUMMARY[@]}"

if [ "$FAILURES" -ne 0 ]; then
  printf '\nnegative-compile: %d failed, %d skipped.\n' "$FAILURES" "$SKIPS"
  exit 1
fi
if [ "$SKIPS" -ne 0 ]; then
  printf '\nnegative-compile: all run legs passed, but %d were SKIPPED and their branch is uncovered.\n' "$SKIPS"
  exit 0
fi
printf '\nnegative-compile: every leg passed.\n'
