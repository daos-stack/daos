#!/bin/bash
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Shared helpers sourced by the other utils/build/*.sh scripts.
# Not meant to be executed directly.

# Scans "$@" for -h/--help and, if found, calls the sourcing script's usage()
# function and exits 0. Must be called after usage() is defined by the caller.
check_help() {
    local arg
    for arg in "$@"; do
        case "${arg}" in
            -h | --help)
                usage
                exit 0
                ;;
        esac
    done
}

check_scons() {
    [[ -n $(command -v scons 2>/dev/null) ]] || {
        echo "ERROR: 'scons' not found on PATH." >&2
        exit 1
    }
}

# Maps the running OS (from /etc/os-release) to the DISTRO suffix used by
# DAOS RPMs;
detect_distro() {
    # shellcheck source=/dev/null
    (. /etc/os-release
    case "${ID:-}" in
        rocky | rhel | centos | almalinux)
            echo "el${VERSION_ID%%.*}"
            ;;
        opensuse-leap | sles)
            echo "suse.lp${VERSION_ID//./}"
            ;;
        *)
            echo "ERROR: cannot auto-detect DISTRO for ID=${ID:-unknown}; set it explicitly" >&2
            return 1
            ;;
    esac
    )
}