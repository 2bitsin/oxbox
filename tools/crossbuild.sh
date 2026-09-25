#!/usr/bin/env bash
# Run one CI cross lane here, in its own image, against the working tree; it
# needs the lane's image variable, a buildutil source, and CONAN_REMOTE_*.
set -euo pipefail

lane=${1:-}
shift || true

case "$lane" in
  windows)
    image=${OXBOX_MSVC_IMAGE:?set OXBOX_MSVC_IMAGE to the msvc-wine image}
    # _CL_ appends where CL would replace: /Z7 keeps debug info in the .obj so
    # mspdbsrv, which hangs under wine, never spawns. /root/.wine is unwritable.
    env_args=(-e "_CL_=/Z7" -e "WINEPREFIX=/w/_crossbuild-$lane/.wine")
    # The prefix is built once and kept; `wineserver -p` exits 2 with one up.
    setup='mkdir -p "$WINEPREFIX"
           wineserver -p
           trap "wineserver -k >/dev/null 2>&1 || true" EXIT
           wine wineboot >/dev/null 2>&1 || true'
    cmd=(buildutil --jobs 7 build --release --no-tests --skip-dependency-upload-so-everyone-rebuilds-from-source)
    ;;
  macos)
    image=${OXBOX_OSXCROSS_IMAGE:?set OXBOX_OSXCROSS_IMAGE to the osxcross image}
    env_args=()
    setup='. /w/tools/osxcross-env.sh'
    cmd=(buildutil --jobs 7 build --release --no-tests --compiler osxcross --skip-dependency-upload-so-everyone-rebuilds-from-source)
    ;;
  *)
    echo "usage: $0 <macos|windows> [buildutil args...]" >&2
    exit 2 ;;
esac

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
docker=${DOCKER:-docker}          # DOCKER=podman, or a remote docker

home=$repo/_crossbuild-$lane   # wine refuses a prefix under a dir it does not own
mkdir -p "$home"

mounts=(-v "$repo:/w")
if [ -n "${BUILDUTIL_WHEEL:-}" ]; then
  wheel=$(cd -- "$(dirname -- "$BUILDUTIL_WHEEL")" && pwd)/$(basename -- "$BUILDUTIL_WHEEL")
  [ -d "$wheel" ] || wheel=$(dirname -- "$wheel")   # pip needs the real filename
  mounts+=(-v "$wheel:/wheels:ro")
  install='pip install -q --no-index --find-links /wheels buildutil'
elif [ -n "${BUILDUTIL_INDEX:-}" ]; then
  install='pip install -q --index-url "$BUILDUTIL_INDEX" buildutil'
  env_args+=(-e BUILDUTIL_INDEX)
else
  echo "$0: set BUILDUTIL_WHEEL or BUILDUTIL_INDEX" >&2
  exit 2
fi

# --entrypoint bash: both images have `sudo` for an ENTRYPOINT, which drops
# every -e. --network host: the conan remote may need the host's namespace.
exec "$docker" run --rm -i \
  --user "$(id -u):$(id -g)" \
  -e "HOME=/w/_crossbuild-$lane" \
  "${mounts[@]}" \
  -e "BUILDUTIL_VENV_DIR=/tmp/_pyvenv" \
  -e "CONAN_HOME=/w/_conanhome-$lane" \
  -e CONAN_REMOTE_URL -e CONAN_REMOTE_NAME \
  -e CONAN_REMOTE_USER -e CONAN_REMOTE_PASS \
  ${env_args[@]+"${env_args[@]}"} \
  --network host \
  --entrypoint bash "$image" -c '
    set -euo pipefail
    '"$setup"'
    python3 -m venv /tmp/pkgvenv
    export PATH="/tmp/pkgvenv/bin:$PATH"
    '"$install"'
    cd /w
    exec "$@"' -- "${cmd[@]}" "$@"
