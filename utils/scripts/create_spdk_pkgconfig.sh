#!/bin/bash

prefix="$1"

mkdir -p "$1/lib64/pkgconfig"
cat << EOF > "$1/lib64/pkgconfig/daos_spdk.pc"
libdir=${prefix}/lib64/daos_srv
includedir=${prefix}/include/daos_srv

Name: daos_spdk
Description:  DAOS version of spdk
URL: https://spdk.io
Version: 1.0.0
Requires:
Cflags: -I\${includedir}
Libs: -L\${libdir} -Wl,-rpath-link=\${libdir}
EOF
