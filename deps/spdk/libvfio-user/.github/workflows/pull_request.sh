#!/bin/sh

# NB: we can't use "meson compile -v", as centos 7 doesn't support that option.

set -e
set -v

test -z "$BUILD" && BUILD=build

rm -rf $BUILD

# ASAN build with clang
CC=clang meson setup $BUILD/asan -Db_sanitize=address -Db_lundef=false
ninja -C $BUILD/asan -v
meson test -C $BUILD/asan --suite style --print-errorlogs
meson test -C $BUILD/asan --no-suite style --print-errorlogs

# analyzer build
meson setup $BUILD/scan-build -Dtran-pipe=true
# no "meson scan-build" for some reason
ninja -C $BUILD/scan-build -v scan-build -v
meson test -C $BUILD/scan-build --no-suite style --print-errorlogs

# debug build with clang
CC=clang meson setup build/clang-debug -Dtran-pipe=true
ninja -C $BUILD/clang-debug -v
meson test -C $BUILD/clang-debug --no-suite style --print-errorlogs

# release build with clang
CC=clang meson setup build/clang-release -Dtran-pipe=true -Dbuildtype=release
ninja -C $BUILD/clang-release -v
meson test -C $BUILD/clang-release --no-suite style --print-errorlogs

# debug build with gcc
CC=gcc meson setup build/gcc-debug -Dtran-pipe=true
ninja -C $BUILD/gcc-debug -v
meson test -C $BUILD/gcc-debug --no-suite style --print-errorlogs

meson test -C $BUILD/gcc-debug --suite unit --setup valgrind --print-errorlogs
meson test -C $BUILD/gcc-debug --suite pyunit --setup pyvalgrind --print-errorlogs

DESTDIR=tmp.install meson install -C $BUILD/gcc-debug

# release build with gcc
CC=gcc meson setup build/gcc-release -Dtran-pipe=true -Dbuildtype=release
ninja -C $BUILD/gcc-release -v
meson test -C $BUILD/gcc-release --no-suite style --print-errorlogs
