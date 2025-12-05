#!/bin/bash

set -e

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <path>"
  exit 1
fi

prefix=$1

if [ ! -d "$prefix" ]; then
  echo "Error: Directory '$prefix' not found."
  exit 1
fi

# Move prefix/include/fuse3 to prefix/include/fused
if [ -d "$prefix/include/fuse3" ]; then
  mv "$prefix/include/fuse3" "$prefix/include/fused"
  echo "Moved $prefix/include/fuse3 to $prefix/include/fused"
else
  echo "Error: $prefix/include/fuse3 not found."
  exit 1
fi

# Find libfuse3.a and rename it to libfused.a
libfuse_path=$(find "$prefix" -name "libfuse3.a" | head -n 1)

if [ -n "$libfuse_path" ]; then
  libfused_path="$(dirname "$libfuse_path")/libfused.a"
  mv "$libfuse_path" "$libfused_path"
  echo "Renamed $libfuse_path to $libfused_path"

  # find pkgconfig/fuse3.pc in the same folder as libfuse3.a and rename it to fused.pc
  pkgconfig_dir=$(dirname "$libfuse_path")/pkgconfig
  if [ -f "$pkgconfig_dir/fuse3.pc" ]; then
    mv "$pkgconfig_dir/fuse3.pc" "$pkgconfig_dir/fused.pc"
    echo "Renamed $pkgconfig_dir/fuse3.pc to $pkgconfig_dir/fused.pc"

    # replace fuse3 with fused
    sed -i 's/fuse3/fused/' "$pkgconfig_dir/fused.pc"
    echo "Updated $pkgconfig_dir/fused.pc"
  else
    echo "Error: fuse3.pc not found in $pkgconfig_dir"
    exit 1
  fi
  exit 0
fi

echo "Error: libfuse3.a not found in $prefix."
exit 1
