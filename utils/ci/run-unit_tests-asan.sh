#!/bin/bash
# Run DAOS unit tests under AddressSanitizer and collect all reports.
#
# Exit codes written to /home/daos/test-results/exit_status:
#   asan_exit=<N>        Raw exit code from the test runner (42 = ASan abort)
#   functional_failure=1 Set when tests fail for non-ASan reasons
#   asan_detected=1      Set when ASan reports were written to asan-logs/
#
# Outputs mounted back to the host runner:
#   /home/daos/asan-logs/      – per-PID ASan log files  (asan.<pid>)
#   /home/daos/test-results/   – JUnit XML files + exit_status

set -uo pipefail

# ── Directories shared with the host via Docker volumes ──────────────────────
ASAN_LOG_DIR=/home/daos/asan-logs
RESULTS_DIR=/home/daos/test-results
mkdir -p "${ASAN_LOG_DIR}" "${RESULTS_DIR}"

# ── Mount tmpfs for PMDK / VOS tests ─────────────────────────────────────────
mkdir -p /mnt/daos
mount -t tmpfs \
	-o rw,noatime,inode64,huge=always,mpol=prefer:0,uid="$(id -u)",gid="$(id -g)" \
	tmpfs /mnt/daos

cd daos
# shellcheck source=/dev/null
source utils/sl/setup_local.sh

# ── ASan runtime options ──────────────────────────────────────────────────────
# log_path:    write one file per PID so reports are never interleaved
# exitcode=42: lets the workflow distinguish ASan aborts from functional fails
# symbolize=1: resolve addresses to file:line in the log (requires debug info)
export ASAN_OPTIONS="log_path=${ASAN_LOG_DIR}/asan:exitcode=42:print_summary=1:symbolize=1"

# ── Run the full unit-test suite ──────────────────────────────────────────────
# --gha     : emit ::group:: / ::endgroup:: markers for GitHub Actions log folding
# --sudo no : container already runs as root; no nested sudo needed
# --no-fail-on-error : we collect the exit code ourselves below so that every
#                      test suite runs even when an earlier one fails
export CMOCKA_XML_FILE="${RESULTS_DIR}/cmocka-%g.xml"
export PMEMOBJ_CONF="sds.at_create=0"

set +e   # do not abort on test failure; we capture the exit code
python3 utils/run_utest.py \
	--gha \
	--sudo no \
	--no-fail-on-error \
	--log_dir "${RESULTS_DIR}/logs"
RUNNER_RC=$?
set -e

# ── Classify the exit ─────────────────────────────────────────────────────────
ASAN_DETECTED=0
if ls "${ASAN_LOG_DIR}"/asan.* >/dev/null 2>&1; then
	ASAN_DETECTED=1
fi

FUNCTIONAL_FAILURE=0
# A non-zero exit that is NOT solely due to ASan (exit 42) counts as a
# functional failure.  If ASan aborts the process it always sets exit 42;
# run_utest.py propagates that.  Any other non-zero value means at least one
# test failed for a non-memory-safety reason.
if [ "${RUNNER_RC}" -ne 0 ] && [ "${RUNNER_RC}" -ne 42 ]; then
	FUNCTIONAL_FAILURE=1
fi
# Also treat exit 42 WITH no asan log files as a functional failure (edge case
# where a test binary itself exits with 42).
if [ "${RUNNER_RC}" -eq 42 ] && [ "${ASAN_DETECTED}" -eq 0 ]; then
	FUNCTIONAL_FAILURE=1
fi

# ── Write a structured status file readable by the workflow ──────────────────
cat > "${RESULTS_DIR}/exit_status" <<EOF
runner_rc=${RUNNER_RC}
asan_detected=${ASAN_DETECTED}
functional_failure=${FUNCTIONAL_FAILURE}
EOF

echo "=== Test run complete ==="
echo "  runner_rc=${RUNNER_RC}"
echo "  asan_detected=${ASAN_DETECTED}"
echo "  functional_failure=${FUNCTIONAL_FAILURE}"

# Always exit 0 here so Docker returns control to the workflow, which
# will inspect exit_status and fail the job with a meaningful message.
exit 0
