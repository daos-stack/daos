#!/bin/bash
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

usage() {
    cat <<EOF
Usage: ${0##*/} [SCONS_OPTION]... [VARIABLE=VALUE]...

Build DAOS with scons, assuming the dependencies are already built.

The script always runs:

    scons install --config=force --build-deps=no [defaults] "\$@"

and applies these defaults unless the same option or variable is given on the
command line:

    --jobs <nproc>      number of parallel jobs
    USE_INSTALLED=all   reuse dependencies already installed on the system
    PREFIX=/opt/daos    installation prefix

Options:
    -c                  run scons -c command
    -h, --help          show this help and exit

Any other argument is forwarded verbatim to scons, e.g.:

    ${0##*/} BUILD_TYPE=debug COMPILER=clang --jobs 8
EOF
}

jobs_set=false
prefix_set=false
use_installed_set=false
install_set=false
for arg in "$@"; do
    case "$arg" in
        -c)
            scons -c
            exit 0
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        -j | --jobs )
            jobs_set=true
            ;;
        PREFIX=*)
            prefix_set=true
            ;;
        USE_INSTALLED=*)
            use_installed_set=true
            ;;
        install)
            install_set=true
            ;;
        --build-deps=*)
            build_set=true
            ;;
    esac
done

SCONS_ARGS=()
if ! "$install_set"; then
    SCONS_ARGS+=(install)
fi
if ! "$build_set"; then
    SCONS_ARGS+=(--build-deps=no )
fi
if ! "$jobs_set"; then
    SCONS_ARGS+=(--jobs "$(nproc)")
fi
if ! "$use_installed_set"; then
    SCONS_ARGS+=("USE_INSTALLED=all")
fi
if ! "$prefix_set"; then
    SCONS_ARGS+=("PREFIX=/opt/daos")
fi
SCONS_ARGS+=("$@")

echo "Building DAOS using scons with args:"
echo "${SCONS_ARGS[*]}"

scons "${SCONS_ARGS[@]}"
