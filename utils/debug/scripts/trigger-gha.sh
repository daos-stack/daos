#!/bin/bash
# Trigger the DAOS unit-testing GHA workflow (standard/asan/ubsan/tsan, all
# 4 jobs together) on this ticket's own worktree branch without waiting for
# the full CI pipeline.
#
# Usage: trigger-gha.sh
#
# Note: asan.yml/ubsan.yml/tsan.yml no longer exist -- they were merged
# into a single unit-testing.yml (which runs all 4 jobs via a shared
# reusable workflow, unit-test-template.yml). unit-testing.yml's
# workflow_dispatch trigger takes no inputs, so individual jobs can no
# longer be triggered separately; triggering it always runs all 4.

set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

source "$CWD/env.sh"

BRANCH=$(git -C "$DAOS_SRC" rev-parse --abbrev-ref HEAD)

echo "Branch: ${BRANCH}"
echo "Triggering: unit-testing.yml (standard + asan + ubsan + tsan)"
echo ""

gh workflow run unit-testing.yml \
	--repo daos-stack/daos \
	--ref "${BRANCH}"

echo "unit-testing.yml triggered."
echo ""
echo "Monitor workflow runs with:"
echo "  gh run list --repo daos-stack/daos --branch ${BRANCH}"
echo "  gh run watch --repo daos-stack/daos"
