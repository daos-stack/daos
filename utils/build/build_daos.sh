#!/bin/bash
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

usage() {
    cat <<EOF
Usage: ${0##*/} [SCONS_OPTIONS]
       ${0##*/} -c | --clean | -f | --full-clean
       ${0##*/} -h | --help

Build DAOS with scons, assuming the dependencies are already installed or built.
The script supplies the defaults listed below; all other options use SCons' own
defaults unless explicitly overridden.

For normal build, the script always runs \`scons install [defaults] "\$@"\`

The following defaults apply unless overridden:
    --jobs \$(nproc)     Use all available cores for parallel jobs
    USE_INSTALLED=all   Use installed dependencies
    PREFIX=/opt/daos    Install under /opt/daos

Options:
    -c, --clean         Run `scons -c` and remove generated build state
    -f, --full-clean    Same as `--clean` but also remove saved build configuration
    -h, --help          Show this help and exit

Any other argument is forwarded verbatim to scons, e.g.:

    ${0##*/} BUILD_TYPE=debug COMPILER=clang --jobs 8
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=utils/build/build_utils.sh
source "${script_dir}/build_utils.sh"

check_scons

jobs_set=false
prefix_set=false
use_installed_set=false
install_set=false

clean_scons() {
    scons -c
    rm -rf .sconf_temp .sconsign.dblite config.log build
    find site_scons -type d -name __pycache__ -prune -exec rm -rf {} + 2>/dev/null || true
}

full_clean_scons() {
    clean_scons
    rm -f .build_vars.json .build_vars.sh daos.conf
}

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
            full_clean_scons
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
    esac
done

SCONS_ARGS=()
if ! "$install_set"; then
    SCONS_ARGS+=(install)
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
