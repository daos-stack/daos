#!/bin/bash

# Does not support wildcards but does move whole directories
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
    utils/rpms/move_files.sh "${src}" "${dest}" "${oldprefix}" "${newprefix}" "${lib}" \
                             "${libdir}" $(basename -a "${src}/"*)
    rmdir "${src}"
    shift
    continue
  fi

  grep -IL '' "${src}" | xargs -I % patchelf --remove-rpath '%'
  grep -Il '' "$src" | xargs -I % sed -i "s!${oldprefix}/${lib}!${libdir}!" '%'
  grep -Il '' "$src" | xargs -I % sed -i "s!${oldprefix}/!${newprefix}!" '%'
  grep -Il '' "$src" | xargs -I % sed -i "s!-L${oldprefix}\S*!!" '%'
  dbg_src=$(echo "${src}" | sed "s!${oldprefix}!/usr/lib/debug/${oldprefix}!")
  dbg_dest=$(echo "${dest_root}" | sed "s!${newprefix}/bin!${newprefix}/lib/debug/${newprefix}/bin!")
  dbg_dest=$(echo "${dest_root}" | sed "s!${newprefix}/lib!${newprefix}/lib/debug/${newprefix}/lib!")
  fname=$(grep -IL '' "${dbg_src}-"*".debug")
  if [ -f "${fname}" ]; then
    mkdir -p "${dbg_dest}"
    mv "${fname}" "${dbg_dest}"
  fi
  mv "${src}" "${dest_root}"
  shift
done
