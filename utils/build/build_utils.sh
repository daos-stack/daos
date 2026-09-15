#!/bin/bash
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Shared helpers sourced by the other utils/build/*.sh scripts.
# Not meant to be executed directly.

check_scons() {
    [[ -n $(command -v scons 2>/dev/null) ]] || {
        echo "ERROR: 'scons' not found on PATH." >&2
        exit 1
    }
}

# Maps the running OS (from /etc/os-release) to the RPM suffix used by DAOS;
detect_rpm_suffix() {
    # shellcheck source=/dev/null
    (. /etc/os-release
    case "${ID:-}" in
        rocky | rhel | almalinux)
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

validate_rpm_suffix() {
    case "${1}" in
        el9 | suse.lp15*)
            ;;
        *)
            echo "ERROR: unsupported RPM suffix ${1} \
(expected e.g. el9, suse.lp15*)"
            exit 1
            ;;
    esac
}
