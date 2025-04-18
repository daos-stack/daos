#!/bin/bash

# Removes the buildbase from any runtime or config file paths
buildbase="$1"; shift
fixroot="$1"; shift
action="$1"; shift

run_patchelf()
{
  set -x
  echo "Patching $1"
  if [ "${action}" = "remove-rpath" ]; then
    patchelf --remove-rpath "$1"
    strip "$1"
    return
  fi
  rpath=$(patchelf --print-rpath "$1")
  if [[ ${rpath} =~ ${buildbase} ]]; then
    if [ -n "${rpath}" ]; then
      # shellcheck disable=SC2001
      patched_rpath=$(sed "s!${buildbase}!!g" <<< "${rpath}")
      patchelf --set-rpath "${patched_rpath}" "$1"
      strip "$1"
    fi
  fi
}

export -f run_patchelf
export buildbase
export action
grep -IrL '' "${fixroot}" | xargs -I % bash -c "run_patchelf '%'"
grep -Irl '' "${fixroot}" | xargs -I % sed -i "s!${buildbase}!!g" '%'
