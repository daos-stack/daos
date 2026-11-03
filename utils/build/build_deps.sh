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
Usage: ${0##*/} [-f|--force] [--build-range=deps|test-deps|all] [SCONS_OPTION ...]
       ${0##*/} -h | --help

Build ordinary or test dependencies with scons. The script supplies the defaults
listed below; all other options use SCons' own defaults unless explicitly
overridden.

The deps range runs \`scons install --build-deps=only [defaults] [SCONS_OPTION ...]\`.
The test-deps range uses \`--build-test-deps=only\` instead.
The all range uses \`--build-deps=only --build-test-deps=yes\` to build both groups.

The following defaults apply unless overridden:
    --build-range=deps  Build only the ordinary dependencies of DAOS
    --jobs \$(nproc)    Use all available cores for parallel jobs
    USE_INSTALLED=all   Use installed dependencies
    PREFIX=/opt/daos    Install under /opt/daos

Options:
    -f, --force         Wipe all dependency directories before building,
                        regardless of range; must be the first argument.
    --build-range       deps (default), test-deps, or all (both groups).
                        The deps range does not enable test dependencies.
    -h, --help          Show this help and exit

Any other argument is forwarded verbatim to scons, e.g.:

    ${0##*/} TARGET_TYPE=debug DEPS=ofi --jobs 8
    ${0##*/} --build-range=test-deps TEST_DEPS=slurm --jobs 8
    ${0##*/} --build-range=all DEPS=ofi TEST_DEPS=slurm --jobs 8
EOF
}

jobs_set=false
prefix_set=false
prefix_value="/opt/daos"
use_installed_set=false
force=false
build_range=deps
scons_options=()

if [ "${1:-}" = "-f" ] || [ "${1:-}" = "--force" ]; then
    force=true
fi

for arg in "$@"; do
    case "$arg" in
        -h | --help)
            usage
            exit 0
            ;;
        --build-range=*)
            build_range="${arg#*=}"
            continue
            ;;
        -f | --force)
            if [[ "${force}" != true ]]; then
                echo 'ERROR: --force must be the first argument' >&2
                exit 1
            fi
            continue
            ;;
        -j | -j[0-9]* | --jobs | --jobs=*)
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
    scons_options+=("${arg}")
done

case "${build_range}" in
    deps | test-deps | all) ;;
    *) echo 'ERROR: --build-range must be deps, test-deps, or all' >&2; exit 1 ;;
esac
check_scons

if [ "$force" = true ]; then
    echo "Removing \"${prefix_value}/prereq\""
    rm -rf "${prefix_value}/prereq"
    echo "Removing \"build/external\""
    rm -rf "build/external"
fi

SCONS_ARGS=(install)
if [[ "${build_range}" = test-deps ]]; then
    SCONS_ARGS+=(--build-test-deps=only)
else
    SCONS_ARGS+=(--build-deps=only)
    if [[ "${build_range}" = all ]]; then
        SCONS_ARGS+=(--build-test-deps=yes)
    fi
fi
if ! "$jobs_set"; then
    SCONS_ARGS+=(--jobs "$(nproc)")
fi
if ! "$use_installed_set"; then
    SCONS_ARGS+=("USE_INSTALLED=all")
fi
if ! "$prefix_set"; then
    SCONS_ARGS+=("PREFIX=${prefix_value}")
fi
SCONS_ARGS+=("${scons_options[@]}")

echo "Building ${build_range} using scons with args:"
echo "${SCONS_ARGS[*]}"

scons "${SCONS_ARGS[@]}"
