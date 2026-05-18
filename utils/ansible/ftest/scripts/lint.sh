#!/usr/bin/env bash
# Run yamllint and ansible-lint on the DAOS ftest Ansible playbook and roles.
#
# Usage:
#   scripts/lint.sh [--yaml-only | --ansible-only] [PATH...]
#
# Examples:
#   scripts/lint.sh                         # lint everything
#   scripts/lint.sh --yaml-only             # yamllint only
#   scripts/lint.sh --ansible-only          # ansible-lint only
#   scripts/lint.sh roles/daos_server       # lint a single role
#
# Requirements:
#   pip install yamllint ansible-lint

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FTEST_DIR="$(dirname "${SCRIPT_DIR}")"
REPO_ROOT="$(cd "${FTEST_DIR}/../../.." && pwd)"

YAMLLINT_CFG="${REPO_ROOT}/.yamllint.yaml"
ANSIBLE_LINT_PROFILE="min"

RUN_YAML=true
RUN_ANSIBLE=true
EXTRA_PATHS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --yaml-only)    RUN_ANSIBLE=false; shift ;;
        --ansible-only) RUN_YAML=false;    shift ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
        *) EXTRA_PATHS+=("$1"); shift ;;
    esac
done

# Default target paths relative to FTEST_DIR
if [[ ${#EXTRA_PATHS[@]} -eq 0 ]]; then
    TARGET_YAML=("${FTEST_DIR}")
    TARGET_ANSIBLE=("${FTEST_DIR}/ftest.yml" "${FTEST_DIR}/roles")
else
    TARGET_YAML=("${EXTRA_PATHS[@]}")
    TARGET_ANSIBLE=("${EXTRA_PATHS[@]}")
fi

RC=0

# ── yamllint ──────────────────────────────────────────────────────────────────
if ${RUN_YAML}; then
    if ! command -v yamllint &>/dev/null; then
        echo "ERROR: yamllint not found. Install it with: pip install yamllint" >&2
        exit 1
    fi
    echo "──────────────────────────────────────────────"
    echo "  yamllint"
    echo "──────────────────────────────────────────────"
    YAMLLINT_ARGS=()
    if [[ -f "${YAMLLINT_CFG}" ]]; then
        YAMLLINT_ARGS+=("--config-file" "${YAMLLINT_CFG}")
    fi
    if yamllint "${YAMLLINT_ARGS[@]}" "${TARGET_YAML[@]}"; then
        echo "✓ yamllint passed"
    else
        RC=$?
    fi
fi

# ── ansible-lint ──────────────────────────────────────────────────────────────
if ${RUN_ANSIBLE}; then
    if ! command -v ansible-lint &>/dev/null; then
        echo "ERROR: ansible-lint not found. Install it with: pip install ansible-lint" >&2
        exit 1
    fi
    echo ""
    echo "──────────────────────────────────────────────"
    echo "  ansible-lint (profile: ${ANSIBLE_LINT_PROFILE})"
    echo "──────────────────────────────────────────────"
    if (cd "${FTEST_DIR}" && ansible-lint --profile "${ANSIBLE_LINT_PROFILE}" "${TARGET_ANSIBLE[@]}"); then
        echo "✓ ansible-lint passed"
    else
        RC=$?
    fi
fi

exit ${RC}
