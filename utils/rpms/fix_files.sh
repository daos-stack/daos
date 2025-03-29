#!/bin/bash

# Removes the buildbase from any runtime or config file paths
buildbase="$1"; shift
fixroot="$1"; shift

run_patchelf()
{
  rpath=$(patchelf --print_rpath "$1")
  if [ -n "${rpath}" ]; then
    patched_rpath=$(echo "${rpath}" | sed "s!${buildbase}!!g")
    patchelf --set-rpath "${patched_rpath}" "$1"
  fi
}

grep -IrL '' "${src}" | xargs -I % run_patchelf '%'
grep -Irl '' "$src" | xargs -I % sed -i "s!${buildbase}!!g" '%'
