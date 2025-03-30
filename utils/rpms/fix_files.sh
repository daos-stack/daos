#!/bin/bash

# Removes the buildbase from any runtime or config file paths
buildbase="$1"; shift
fixroot="$1"; shift

run_patchelf()
{
  set -x
  echo "Patching $1"
  rpath=$(patchelf --print-rpath "$1")
  if [ -n "${rpath}" ]; then
    patched_rpath=$(echo "${rpath}" | sed "s!${buildbase}!!g")
    patchelf --set-rpath "${patched_rpath}" "$1"
  fi
}

export -f run_patchelf
grep -IrL '' "${fixroot}" | xargs -I % bash -c "run_patchelf '%'"
grep -Irl '' "${fixroot}" | xargs -I % sed -i "s!${buildbase}!!g" '%'
