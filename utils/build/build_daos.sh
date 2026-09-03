#!/bin/bash
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

usage() {
    cat <<EOF
Usage: ${0##*/} [-w | --wipe] [SCONS_PARAMETERS]
       ${0##*/} -c | --clean | -f | --full-clean
       ${0##*/} -h | --help

Build DAOS and/or its dependencies with scons. The script supplies the defaults
listed below; all other options use SCons' own defaults unless explicitly
overridden.

For normal build, the script always runs \`scons install [defaults] "\$@"\`

The following defaults apply unless overridden:
    --jobs \$(nproc)    Use all available cores for parallel jobs
    USE_INSTALLED=all   Use installed dependencies
    PREFIX=/opt/daos    Install under /opt/daos

Options:
    -w, --wipe          Wipe dependency directories before building; use
                        \`TARGET_TYPE=debug|release|dev\` or
                        \`BUILD_TYPE=debug|release|dev\` to select the directories.
                        If omitted, \`release\` directories are wiped.
                        Ignored when scons parameter --build-deps=no.
    -c, --clean         Run 'scons -c' and remove generated build state
    -f, --full-clean    Run 'scons -c' and remove saved build configuration
    -h, --help          Show this help and exit

Any other argument is forwarded verbatim to scons, e.g.:

    ${0##*/} BUILD_TYPE=debug COMPILER=clang --jobs \$(nproc)
    ${0##*/} --build-deps=only TARGET_TYPE=debug DEPS=ofi
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=utils/build/build_utils.sh
source "${script_dir}/build_utils.sh"

check_scons() {
    [[ -n $(command -v scons 2>/dev/null) ]] || {
        echo "ERROR: 'scons' not found on PATH." >&2
        exit 1
    }
}
check_scons

wipe_set=false
install_set=false
build_deps_set=false
build_deps=""
jobs_set=false
prefix_set=false
use_installed_set=false
build_type="release"
target_type="default"
forward_args=()

clean_scons() {
    scons -c
    rm -rf .sconf_temp .sconsign.dblite config.log build
    find site_scons -type d -name __pycache__ -prune -exec rm -rf {} + 2>/dev/null || true
    [ "${1:-"no"}" = "no" ] || {
        rm -f .build_vars.json .build_vars.sh daos.conf
    }
}

clean_deps() {
    local ttype="$1"
    [ "$ttype" = "debug" ] || [ "$ttype" = "release" ] || [ "$ttype" = "dev" ] || \
        ttype="$2"

    [ "$ttype" = "debug" ] || [ "$ttype" = "release" ] || [ "$ttype" = "dev" ] || {
        echo "Invalid build type: $ttype" >&2
        exit 1
    }

    echo "Removing \"${prefix_value}/prereq/${ttype}\""
    rm -rf "${prefix_value}/prereq/${ttype}"
    echo "Removing \"build/external/${ttype}\""
    rm -rf "build/external/${ttype}"
}

if [ "${1:-}" = "-w" ] || [ "${1:-}" = "--wipe" ]; then
    wipe_set=true
    shift
fi

for arg in "$@"; do
    case "$arg" in
        -h | --help)
            usage
            exit 0
            ;;
        -c | --clean)
            clean_scons
            exit 0
            ;;
        -f | --full-clean)
            clean_scons "yes"
            exit 0
            ;;
        --build-deps=*)
            build_deps="${arg#--build-deps=}"
            case "${build_deps}" in
                no|yes|only)
                forward_args+=("$arg")
                ;;
                *)
                    echo "ERROR: --build-deps must be no, yes, or only (got: ${build_deps})" >&2
                    exit 1
                ;;
            esac
            ;;
        -j | --jobs )
            jobs_set=true
            forward_args+=("$arg")
            ;;
        PREFIX=*)
            prefix_set=true
            forward_args+=("$arg")
            ;;
        USE_INSTALLED=*)
            use_installed_set=true
            forward_args+=("$arg")
            ;;
        BUILD_TYPE=*)
            build_type="${arg#BUILD_TYPE=}"
            forward_args+=("$arg")
            ;;
        TARGET_TYPE=*)
            target_type="${arg#TARGET_TYPE=}"
            forward_args+=("$arg")
            ;;
        install)
            install_set=true
            forward_args+=("$arg")
            ;;
        *)
            forward_args+=("$arg")
            ;;
    esac
done

[ "$wipe_set" = true ] && [ "${build_deps}" != "no" ] && \
clean_deps "$target_type" "$build_type"

SCONS_ARGS=()
[ "$install_set" = true ] || SCONS_ARGS+=(install)
[ "$jobs_set" = true ] || SCONS_ARGS+=(--jobs "$(nproc)")
[ "$use_installed_set" = true ] || SCONS_ARGS+=("USE_INSTALLED=all")
[ "$prefix_set" = true ] || SCONS_ARGS+=("PREFIX=/opt/daos}")
SCONS_ARGS+=("${forward_args[@]}")

echo "Building DAOS using scons with args:"
echo "${SCONS_ARGS[*]}"

scons "${SCONS_ARGS[@]}"
