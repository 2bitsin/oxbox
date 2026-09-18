# The osxcross lane's toolchain environment; sourced, not run.
osxcross_root="${OSXCROSS_ROOT:-/opt/osxcross/target}"

PATH="$osxcross_root/bin:$PATH"          # the job shell resets the image PATH
MACOSX_DEPLOYMENT_TARGET=16.0
CCACHE_COMPILERCHECK=content             # oa64-clang is a shim; mtime serves stale hits

if [ ! -x "$osxcross_root/bin/oa64-clang++" ]; then
  echo "osxcross-env: no oa64-clang++ under $osxcross_root/bin --" \
       "this is not the osxcross image" >&2
  return 1 2>/dev/null || exit 1
fi

export PATH MACOSX_DEPLOYMENT_TARGET CCACHE_COMPILERCHECK
echo "+ osxcross: $(oa64-clang++ --version | head -1)"
