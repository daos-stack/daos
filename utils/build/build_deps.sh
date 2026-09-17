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
Usage: ${0##*/} [-f|--force] [SCONS_OPTION]
       ${0##*/} -h | --help

Build the DAOS dependencies with scons. The script supplies the defaults
listed below; all other options use SCons' own defaults unless explicitly
overridden.

For build, the script always runs \`scons install --build-deps=only [defaults] "\$@"\`

The following defaults apply unless overridden:
    --build-deps=only   Build only the dependencies of DAOS
    --jobs \$(nproc)     Use all available cores for parallel jobs
    USE_INSTALLED=all   Use installed dependencies
    PREFIX=/opt/daos    Install under /opt/daos

Options:
    -f, --force         wipe dependency directories before building; use
                        \`TARGET_TYPE=debug|release|dev\` or
                        \`BUILD_TYPE=debug|release|dev\` to select the directories.
                        If omitted, \`release\` directories are wiped.
                        Must be given as the first argument.
    -h, --help          show this help and exit

Any other argument is forwarded verbatim to scons, e.g.:

    ${0##*/} TARGET_TYPE=debug DEPS=ofi --jobs 8
EOF
}

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
build_type=release
target_type=default

for arg in "$@"; do
    case "$arg" in
        -h | --help)
            usage
            exit 0
            ;;
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
        BUILD_TYPE=*)
            build_type="${arg#BUILD_TYPE=}"
            ;;
        TARGET_TYPE=*)
            target_type="${arg#TARGET_TYPE=}"
            ;;
    esac
done

if "$force"; then
    ttype="$target_type"
    [ "$ttype" = "debug" ] || [ "$ttype" = "release" ] || [ "$ttype" = "dev" ] || \
        ttype="$build_type"

    [ "$ttype" = "debug" ] || [ "$ttype" = "release" ] || [ "$ttype" = "dev" ] || {
        echo "Invalid build type: $ttype" >&2
        exit 1
    }

    echo "Removing \"${prefix_value}/prereq/${ttype}\""
    rm -rf "${prefix_value}/prereq/${ttype}"
    echo "Removing \"build/external/${ttype}\""
    rm -rf "build/external/${ttype}"
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
