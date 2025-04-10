#!/bin/bash

component="$1"
build_root="$2"
prefix="$3"
bindir="$4"
libdir="$5"
includedir="$6"
configdir="$7"
datadir="$8"
buildlib="$9"

prereq_base="${build_root}/opt/daos/prereq"
prereq_root="${prereq_base}/release/${component}"

if [ ! -d "${prereq_root}" ]; then
  echo "no build found for ${component} at ${prereq_root}"
  exit 1
fi

move_files()
{
  if [ -d "${prereq_root}/$1" ]; then
    mkdir -p "${build_root}/$2"
    utils/rpms/move_files.sh "${build_root}" "${prereq_root}/$1" "${build_root}/$2" \
                             "${prereq_root}" "${prefix}" "${buildlib}" "${libdir}" \
                             $(basename -a "${prereq_root}/$1/"*)
  rmdir "${prereq_root}/$1"
  fi
}

move_files "bin" "${bindir}"
move_files "${buildlib}" "${libdir}"
move_files "include" "${includedir}"
move_files "etc" "${configdir}"
move_files "share" "${datadir}"
rmdir "${prereq_root}"
