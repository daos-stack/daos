#!/bin/bash

component="$1"
build_root="$2"
prefix="$3"
execprefix="$4"
bindir="$5"
libdir="$6"
includedir="$7"
configdir="$8"
datadir="$9"
buildlib="${10}"

echo "${buildlib} is buildlib"

prereq_base="${build_root}/opt/daos/prereq"
if [ -d "${prereq_base}/release" ]; then
  prereq_root="${prereq_base}/release/${component}"
elif [ -d "${prereq_base}/debug" ]; then
  prereq_root="${prereq_base}/debug/${component}"
elif [ -d "${prereq_base}/dev" ]; then
  prereq_root="${prereq_base}/dev/${component}"
fi

if [ ! -d "${prereq_root}" ]; then
  echo "no build found for ${component} at ${prereq_root}"
  exit 1
fi

set -x
grep -rIl "${prereq_root}" "${prereq_root}" | \
	xargs -I % sed -i "s!${prereq_root}/${buildlib}!${libdir}!" '%'
grep -rIl "${prereq_root}" "${prereq_root}" | \
	xargs -I % sed -i "s!${prereq_root}!${prefix}!" '%'
grep -rIl "${prereq_root}" "${prereq_root}"| \
	xargs -I % sed -i "s!-L${prereq_base}\S*!!" '%'

if [ -d "${prereq_root}/${buildlib}" ]; then
  find ${prereq_root}/${buildlib} -name "*.so*" -type f -exec patchelf --remove-rpath {} ';'
  mkdir -p "${build_root}/${libdir}"
  cp -r "${prereq_root}/${buildlib}"/* "${build_root}/${libdir}"
fi

if [ -d "${prereq_root}/bin" ]; then
  find ${prereq_root}/bin -type f -exec patchelf --remove-rpath {} ';'
  mkdir -p "${build_root}/${bindir}"
  cp -r "${prereq_root}/bin/"* "${build_root}/${bindir}"
fi
if [ -d "${prereq_root}/include" ]; then
  mkdir -p "${build_root}/${includedir}"
  cp -r "${prereq_root}/include/"* "${build_root}/${includedir}"
fi
if [ -d "${prereq_root}/etc" ]; then
  mkdir -p "${build_root}/${configdir}"
  cp -r "${prereq_root}/etc/"* "${build_root}/${configdir}"
fi
if [ -d "${prereq_root}/share" ]; then
  mkdir -p "${build_root}/${datadir}"
  cp -r "${prereq_root}/share/"* "${build_root}/${datadir}"
fi
