#!/bin/bash

# Does not support wildcards but does move whole directories
buildroot="$1"; shift
src_root="$1"; shift
dest_root="$1"; shift
oldprefix="$1"; shift
newprefix="$1"; shift
lib="$1"; shift
libdir="$1"; shift

while [ $# -gt 0 ]; do
  src="${src_root}/$1"
  if [ -d "${src}" ]; then
    dest="${dest_root}/$1"
    mkdir -p "${dest}"
    utils/rpms/move_files.sh "${buildroot}" "${src}" "${dest}" "${oldprefix}" "${newprefix}" \
                             "${lib}" "${libdir}" $(basename -a "${src}/"*)
    rmdir "${src}"
    shift
    continue
  fi

  set -x
  grep -Il '' "$src" | xargs -I % sed -i "s!${buildroot}!!" '%'
  grep -Il '' "$src" | xargs -I % sed -i "s!${oldprefix}/${lib}!${libdir}!" '%'
  grep -Il '' "$src" | xargs -I % sed -i "s!${oldprefix}/!${newprefix}!" '%'
  grep -Il '' "$src" | xargs -I % sed -i "s!-L${oldprefix}\S*!!" '%'
  binary=1
  for ext in ".pc" ".cmake" ".la"; do
    if [ $(basename "${src}" "${ext}") != $(basename "$src") ]; then
      binary=0
      break
    fi
  done
  if [ ${binary} -eq 0 ]; then
    grep -IL '' "${src}" | xargs -I % patchelf --remove-rpath '%'
    dbg_src=$(sed "s!${oldprefix}!/usr/lib/debug/${oldprefix}!" <<< "${src}")
    dbg_dest=$(sed "s!${newprefix}/bin!${newprefix}/lib/debug/${newprefix}/bin!" <<< "${dest_root}")
    dbg_dest=$(sed "s!${newprefix}/lib!${newprefix}/lib/debug/${newprefix}/lib!" <<< "${dest_root}")
    fname=$(grep -IL '' "${dbg_src}-"*".debug")
    if [ -f "${fname}" ]; then
      mkdir -p "${dbg_dest}"
      mv "${fname}" "${dbg_dest}"
    fi
  else
    # some files seem to get treated as binary
    sed -i "s!${buildroot}!!" "${src}"
    sed -i "s!${oldprefix}/${lib}!${libdir}!" "${src}"
    sed -i "s!${oldprefix}/!${newprefix}!" "${src}"
    sed -i "s!-L${oldprefix}\S*!!" "${src}"
  fi
  mv "${src}" "${dest_root}"
  set +x
  shift
done
