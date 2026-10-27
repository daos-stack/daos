#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
source "${script_dir}/build_utils.sh"

if [[ "${1:-}" = -h || "${1:-}" = --help ]]; then
    printf '%s\n' 'Usage: build_test_deps.sh [SCONS_OPTION ...]' \
        'Build only test dependencies, reusing installed components when available.' \
        'Defaults: --build-test-deps=only, USE_INSTALLED=all, PREFIX=/opt/daos.'
    exit 0
fi

check_scons
scons --build-deps=no --jobs "$(nproc)" \
    PREFIX=/opt/daos USE_INSTALLED=all --build-test-deps=only "$@"