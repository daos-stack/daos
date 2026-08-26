#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# verify_rpms.sh
#
# Validate generated RPM packages produced by ci/rpm/gen_rpms.sh.
#
# Checks:
# - RPM metadata and payload listing are readable.
# - Devel packages require matching non-devel package name and exact version.
# - Packages containing dynamically linked ELF executables require libc.
#
# Validation model:
# - Cache generated package names from all RPMs in the current build set.
# - For each -devel package, require an exact-version dependency on at least
#   one generated runtime package (supports lib* numeric variants like
#   libfoo1/libfoo0).
# - For each RPM, inspect extracted executable ELF payload files; if dynamic,
#   ensure RPM Requires include libc soname dependency (libc.so.6* entries).
#
# Supported RPM layouts:
# - Root with nested subdirs: RPM_ROOT/deps/*.rpm and RPM_ROOT/daos/*.rpm
# - Flat root: RPM_ROOT/*.rpm
#
# Scope and limits:
# - This is a CI consistency gate for generated RPMs in one build set, not a
#   full dependency solver.
# - It intentionally ignores debug package providers when selecting runtime
#   providers to avoid debug-related false positives.
#
# Usage:
#   utils/rpms/verify_rpms.sh [RPM_ROOT]
#
# Arguments:
#   RPM_ROOT  Root directory containing either:
#             - deps/*.rpm and daos/*.rpm, or
#             - *.rpm directly in RPM_ROOT.
#             Default: .
#
# Exit codes:
#   0  Validation passed.
#   1  Validation failed or required tooling is missing.

set -euo pipefail
shopt -s nullglob

RPM_ROOT="${1:-.}"

for tool in rpm rpm2cpio cpio readelf; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "ERROR: ${tool} command is required"
        exit 1
    fi
done

rpms=("${RPM_ROOT}"/deps/*.rpm "${RPM_ROOT}"/daos/*.rpm "${RPM_ROOT}"/*.rpm)
if [ "${#rpms[@]}" -eq 0 ]; then
    echo "ERROR: no RPM files found under ${RPM_ROOT}/deps, ${RPM_ROOT}/daos, or ${RPM_ROOT}"
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

declare -i errors=0
declare -A pkg_names
declare -A pkg_present
declare -A pkg_files

# Check whether RPM Requires contains a regex pattern.
# Args:
#   $1 path to RPM file
#   $2 extended regex pattern
# Returns:
#   0 if pattern exists in Requires output, 1 otherwise
rpm_requires_match() {
    local rpm_file="$1"
    local pattern="$2"

    rpm -qpR "${rpm_file}" | grep -Eq "${pattern}"
}

# Validate basic RPM integrity and cache Requires metadata.
# Args:
#   $1 path to RPM file
# Returns:
#   no direct return value; increments global error counter on failure
check_rpm_basic() {
    local rpm_file="$1"
    local pkg_name

    if ! rpm -K "${rpm_file}" >/dev/null; then
        echo "ERROR: rpm verification failed for ${rpm_file}"
        errors+=1
        return
    fi

    if ! pkg_name="$(rpm -qp --qf '%{NAME}\n' "${rpm_file}")"; then
        echo "ERROR: cannot read package name from ${rpm_file}"
        errors+=1
        return
    fi

    pkg_names["${rpm_file}"]="${pkg_name}"
    pkg_present["${pkg_name}"]=1
    pkg_files["${pkg_name}"]="${rpm_file}"

    if ! rpm -qpl "${rpm_file}" >/dev/null; then
        echo "ERROR: cannot list payload for ${rpm_file}"
        errors+=1
    fi

}

# Extract RPM payload into target directory for local file inspection.
# Args:
#   $1 path to RPM file
#   $2 target extraction directory
# Returns:
#   no direct return value; command failure propagates via set -e
extract_rpm() {
    local rpm_file="$1"
    local target_dir="$2"

    mkdir -p "${target_dir}"
    (
        cd "${target_dir}"
        rpm2cpio "${rpm_file}" | cpio -idmu --quiet
    )
}

# Verify that -devel packages depend on a generated runtime package version.
# Args:
#   $1 path to RPM file
#   $2 package name
# Returns:
#   no direct return value; increments global error counter on mismatch
check_devel_runtime_dependency() {
    local rpm_file="$1"
    local pkg_name="$2"
    local runtime_pkg
    local expected_version
    local require_pattern
    local daos_client_rpm
    local require
    local dep_pkg
    local operator
    local dep_version
    local found_runtime_dep=0

    if ! expected_version="$(rpm -qp --qf '%{VERSION}-%{RELEASE}\n' "${rpm_file}")"; then
        echo "ERROR: cannot determine package version for ${pkg_name} (${rpm_file})"
        errors+=1
        return
    fi

    # fused-devel is intentionally built without fused runtime RPMs in this
    # pipeline, so requiring a generated runtime package would be a false fail.
    if [ "${pkg_name}" = "fused-devel" ]; then
        return
    fi

    runtime_pkg="${pkg_name%-devel}"

    # Handle cases like isa-l_crypto-devel -> libisa-l_crypto
    if [ -z "${pkg_present["${runtime_pkg}"]+x}" ] && [ -n "${pkg_present["lib${runtime_pkg}"]+x}" ]; then
        runtime_pkg="lib${runtime_pkg}"
    fi

    # Primary rule: if base runtime package exists in generated RPM set,
    # require exact version dependency on that package.
    if [ -n "${pkg_present["${runtime_pkg}"]+x}" ]; then
        require_pattern="^${runtime_pkg}[[:space:]]*=[[:space:]]*${expected_version}([[:space:]]|$)"
        if ! rpm_requires_match "${rpm_file}" "${require_pattern}"; then
            # Special case: allow daos-devel -> daos-client when daos-client
            # itself requires the exact daos package version.
            if [ "${pkg_name}" = "daos-devel" ] &&
               rpm_requires_match "${rpm_file}" "^daos-client[[:space:]]*=[[:space:]]*${expected_version}([[:space:]]|$)"; then
                daos_client_rpm="${pkg_files["daos-client"]:-}"
                if [ -n "${daos_client_rpm}" ] &&
                   rpm_requires_match "${daos_client_rpm}" "^daos[[:space:]]*=[[:space:]]*${expected_version}([[:space:]]|$)"; then
                    return
                fi
            fi

            echo "ERROR: ${pkg_name} must require ${runtime_pkg} = ${expected_version}"
            errors+=1
        fi
        return
    fi

    # Fallback: support common library runtime naming variants where base
    # runtime package is absent, e.g. libfoo-devel -> libfoo0/libfoo1.
    # Only accept numeric suffix variants of the same base runtime name.
    if [[ "${runtime_pkg}" != lib* ]]; then
        echo "ERROR: ${pkg_name} requires missing runtime package ${runtime_pkg} in generated RPM set"
        errors+=1
        return
    fi

    while IFS= read -r require; do
        echo "DEBUG: ${pkg_name} requires ${require}"
        read -r dep_pkg operator dep_version _ <<<"${require}"

        dep_pkg="${dep_pkg%,}"
        dep_pkg="${dep_pkg%%(*}"

        [ -n "${dep_pkg}" ] || continue
        [ "${operator:-}" = "=" ] || continue
        [ "${dep_version:-}" = "${expected_version}" ] || continue
        [ -n "${pkg_present["${dep_pkg}"]+x}" ] || continue
        [[ "${dep_pkg}" == *-devel ]] && continue
        [[ "${dep_pkg}" =~ ^${runtime_pkg}[0-9]+$ ]] || continue

        found_runtime_dep=1
        break
    done < <(rpm -qpR "${rpm_file}" || true)

    if [ "${found_runtime_dep}" -ne 1 ]; then
        echo "ERROR: ${pkg_name} must require ${runtime_pkg}<N> = ${expected_version}"
        errors+=1
    fi
}

# Ensure dynamic ELF executable content results in libc Requires metadata.
# Args:
#   $1 path to RPM file
#   $2 package name
#   $3 extraction directory
# Returns:
#   no direct return value; increments global error counter on mismatch
check_binary_requires_libc() {
    local rpm_file="$1"
    local pkg_name="$2"
    local extract_dir="$3"
    local file_path
    local has_dynamic_elf_binary=0

    extract_rpm "${rpm_file}" "${extract_dir}"

    while IFS= read -r file_path; do

        # Treat ELF with an interpreter segment as an executable binary
        # (includes PIE binaries) and require dynamic dependencies.
        if readelf -h "${file_path}" >/dev/null 2>&1 &&
           readelf -l "${file_path}" 2>/dev/null | grep -q 'Requesting program interpreter' &&
           readelf -d "${file_path}" 2>/dev/null | grep -q '(NEEDED)'; then
            has_dynamic_elf_binary=1
            break
        fi
    done < <(find "${extract_dir}" -type f -perm /111 2>/dev/null)

    if [ "${has_dynamic_elf_binary}" -eq 1 ] &&
       ! rpm_requires_match "${rpm_file}" '^libc\.so\.6'; then
        echo "ERROR: ${pkg_name} contains dynamically linked ELF binaries, but RPM"
        echo "ERROR: requires metadata is missing libc soname dependency (libc.so.6*)"
        errors+=1
    fi
}

echo "Verifying ${#rpms[@]} generated RPM(s) from ${RPM_ROOT}"

for rpm_file in "${rpms[@]}"; do
    check_rpm_basic "${rpm_file}"
done

for rpm_file in "${rpms[@]}"; do
    pkg_name="${pkg_names["${rpm_file}"]:-}"
    [ -n "${pkg_name}" ] || continue

    work_dir="${tmpdir}/$(basename "${rpm_file}" .rpm)"
    rm -rf "${work_dir}"

    if [[ "${pkg_name}" == *-devel ]]; then
        check_devel_runtime_dependency "${rpm_file}" "${pkg_name}"
    fi

    check_binary_requires_libc "${rpm_file}" "${pkg_name}" "${work_dir}"
done

if [ "${errors}" -ne 0 ]; then
    echo "RPM validation failed with ${errors} error(s)"
    exit 1
fi

echo "RPM validation passed"
