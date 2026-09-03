#!/bin/bash
# Copyright 2025 Google LLC
# Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=utils/build/build_utils.sh
source "${script_dir}/build_utils.sh"

usage() {
    cat <<EOF
Usage: ${0##*/} [-f|--force] [SCONS_OPTION]... [VARIABLE=VALUE]...

Build the DAOS dependencies with scons.

The script always runs:

    scons install --build-deps=only [defaults] "\$@"

and applies these defaults unless the same option or variable is given on the
command line:

    --jobs <nproc>      number of parallel jobs
    USE_INSTALLED=all   reuse dependencies already installed on the system
    PREFIX=/opt/daos    installation prefix

Options:
    -f, --force         wipe \$PREFIX/prereq/release and build/external/release
                        before building; must be given as the first argument
    -h, --help          show this help and exit

Any other argument is forwarded verbatim to scons, e.g.:

    ${0##*/} TARGET_TYPE=debug DEPS=ofi --jobs 8
EOF
}

check_help "$@"
check_scons

force=false
if [ "${1:-}" = "-f" ] || [ "${1:-}" = "--force" ]; then
    force=true
    shift
fi

jobs_set=false
prefix_set=false
prefix_value="/opt/daos"
use_installed_set=false

for arg in "$@"; do
    case "$arg" in
        -j | --jobs )
            jobs_set=true
            ;;
        PREFIX=*)
            prefix_set=true
            prefix_value="${arg#PREFIX=}"
            ;;
        USE_INSTALLED=*)
            use_installed_set=true
            ;;
    esac
done

if "$force"; then
    echo "Removing \"${prefix_value}/prereq/${BUILD_TYPE:-release}\""
    echo "Removing \"build/external/${BUILD_TYPE:-release}\""
    rm -rf "${prefix_value}/prereq/${BUILD_TYPE:-release}" \
        "build/external/${BUILD_TYPE:-release}"
fi

SCONS_ARGS=(install --build-deps=only)
if ! "$jobs_set"; then
    SCONS_ARGS+=(--jobs "$(nproc)")
fi
if ! "$use_installed_set"; then
    SCONS_ARGS+=("USE_INSTALLED=all")
fi
if ! "$prefix_set"; then
    SCONS_ARGS+=("PREFIX=${prefix_value}")
fi
SCONS_ARGS+=("$@")

echo "Building DAOS dependencies using scons with args:"
echo "${SCONS_ARGS[*]}"

scons "${SCONS_ARGS[@]}"
