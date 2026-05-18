#!/usr/bin/env bash
# Run Molecule tests for one or all DAOS ftest Ansible roles.
#
# Usage:
#   scripts/molecule-test.sh [ROLE] [MOLECULE_ARGS...]
#
# Examples:
#   scripts/molecule-test.sh                    # test all roles sequentially
#   scripts/molecule-test.sh daos_client        # test a single role
#   scripts/molecule-test.sh daos_server --destroy never   # keep container after run
#
# Requirements:
#   pip install molecule molecule-plugins[docker]
#   A local Docker daemon with the daos/rocky-el9:2.8 image available.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FTEST_DIR="$(dirname "${SCRIPT_DIR}")"
ROLES_DIR="${FTEST_DIR}/roles"

usage() {
    sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'
    exit 1
}

run_role() {
    local role="$1"
    shift
    local role_dir="${ROLES_DIR}/${role}"

    if [[ ! -d "${role_dir}" ]]; then
        echo "ERROR: role '${role}' not found under ${ROLES_DIR}" >&2
        exit 1
    fi
    if [[ ! -f "${role_dir}/molecule/default/molecule.yml" ]]; then
        echo "SKIP: ${role} has no Molecule scenario" >&2
        return 0
    fi

    echo ""
    echo "════════════════════════════════════════════════════════════════"
    echo "  Testing role: ${role}"
    echo "════════════════════════════════════════════════════════════════"
    (cd "${role_dir}" && molecule test "$@")
}

# Parse first argument: optional role name (no leading dash)
if [[ $# -ge 1 && "${1}" != -* ]]; then
    ROLE="$1"
    shift
    run_role "${ROLE}" "$@"
else
    # Test all roles that have a Molecule scenario
    FAILED=()
    for role_dir in "${ROLES_DIR}"/*/; do
        role="$(basename "${role_dir}")"
        if run_role "${role}" "$@"; then
            :
        else
            FAILED+=("${role}")
        fi
    done

    echo ""
    if [[ ${#FAILED[@]} -eq 0 ]]; then
        echo "✓ All roles passed."
    else
        echo "✗ Failed roles: ${FAILED[*]}" >&2
        exit 1
    fi
fi
