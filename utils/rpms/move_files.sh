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
    set -x
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
  grep -IL '' "${src}" | xargs -I % patchelf --remove-rpath '%'
  grep -IL '' "${src}" | xargs -I % strip '%'
  dbg_src=$(sed "s!${oldprefix}!/usr/lib/debug/${oldprefix}!" <<< "${src}")
  dbg_dest=$(sed "s!${newprefix}/bin!${newprefix}/lib/debug/${newprefix}/bin!" <<< "${dest_root}")
  dbg_dest=$(sed "s!${newprefix}/lib!${newprefix}/lib/debug/${newprefix}/lib!" <<< "${dest_root}")
  fname=$(grep -IL '' "${dbg_src}-"*".debug")
  if [ -f "${fname}" ]; then
    mkdir -p "${dbg_dest}"
    mv "${fname}" "${dbg_dest}"
  fi
  mv "${src}" "${dest_root}"
  set +x
  shift
done
