#!/bin/sh
# Builds the reference site on a Read the Docs builder.
#
#     bash prog/1stPartyLibs/quirrel/quirrel/doc/build_rtd.sh
#
# The in-browser VM is committed as wasm/quirrel.{js,wasm}, so the usual build is
# python3 and nothing else: seconds, and no toolchain to fetch. The pair carries the
# version it was built from in wasm/quirrel.version, and this rebuilds it only when
# that no longer matches the header - a bumped VM is exactly when the committed one
# stops describing the source it is published beside.
#
# The rebuild may fail: build.py treats the VM as optional, and a blocked download
# or a builder out of time must not take the reference off the air. It says so
# loudly and publishes the pages instead.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
: "${READTHEDOCS_OUTPUT:?this script drives a Read the Docs build}"

version=$(python3 "$HERE/gen/quirrel_version.py")
# tr: the stamp is written on whichever platform last built, and a Windows
# worktree checks it out with CRLF, which $(cat) does not strip.
built=$(tr -d "\r" < "$HERE/wasm/quirrel.version" 2>/dev/null || echo none)

vm() {
  # An RTD container starts with no devtools and keeps nothing between builds, so
  # the SDK goes under $HOME and dies with the container. A build image that
  # already carries one only has to point GDEVTOOL at it.
  : "${GDEVTOOL:=$HOME/devtools}"
  export GDEVTOOL
  # clang -O2 on the VM sources wants a few hundred MB each, and the container is
  # memory-capped, so do not let the core count decide.
  : "${WASM_JOBS:=2}"
  export WASM_JOBS
  # cmake is not in the RTD image, and the wheel needs no root, unlike apt.
  # --only-binary keeps a missing wheel from turning into a source build. An image
  # that already carries cmake is left alone.
  command -v cmake >/dev/null 2>&1 || \
    python3 -m pip install --no-cache-dir --disable-pip-version-check \
            --only-binary=:all: "cmake~=3.31.0" || return 1
  # build_wasm.sh provisions the SDK itself, so the toolchain download
  # happens there - and only on this branch, never on a version match.
  timeout "${QUIRREL_WASM_TIMEOUT:-480}" "$HERE/wasm/build_wasm.sh" --force || return 1
  rm -rf "$GDEVTOOL/emsdk/downloads"      # ~400MB of archives, already unpacked

  # A VM that disagrees with a committed .out is a defect, and this is the only
  # place a freshly built one is exercised. Advisory: it must not un-publish the
  # pages. node comes from the SDK the build just provisioned.
  node=$(ls "$GDEVTOOL"/emsdk/node/*/bin/node 2>/dev/null | head -1)
  if [ -x "$node" ]; then
    "$node" "$HERE/wasm/check_examples.js" ||
      echo "WARNING: the rebuilt VM disagrees with the committed example output"
  else
    echo "WARNING: no node in the SDK, so the rebuilt VM was not checked"
  fi
}

if [ "$built" = "$version" ] && [ -f "$HERE/wasm/quirrel.js" ] \
   && [ -f "$HERE/wasm/quirrel.wasm" ]; then
  echo "in-browser VM: using the committed build for $version"
elif vm; then
  echo "in-browser VM: rebuilt, $built -> $version"
else
  echo "WARNING: the committed VM was built from $built, the sources are $version,"
  echo "WARNING: and it could not be rebuilt here. Publishing without it: every page"
  echo "WARNING: is complete and the Run button will report that it has no VM."
  # A link killed by the timeout can leave the .js without the .wasm, and build.py
  # would copy the one that exists. Neither or both.
  rm -f "$HERE/wasm/quirrel.js" "$HERE/wasm/quirrel.wasm"
fi

# Built into _site and copied: build.py refuses to empty a directory that is not
# one of its own builds, and RTD may have created the output one already. This also
# keeps the published build the same command as the local one.
python3 "$HERE/build.py" --version "$version" --committed-api-dump
mkdir -p "$READTHEDOCS_OUTPUT/html"
cp -a "$HERE/_site/." "$READTHEDOCS_OUTPUT/html/"
