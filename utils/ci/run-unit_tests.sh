#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Copyright 2025-2026 Hewlett Packard Enterprise Development LP.
# Run the DAOS unit-test suite, optionally under a sanitizer, and collect
# all reports. Shared by the standard, ASan, UBSan, and TSan GHA jobs.
#
# Usage:
#   run-unit_tests.sh --standard  (no sanitizer)
#   run-unit_tests.sh --asan      (AddressSanitizer only)
#   run-unit_tests.sh --ubsan     (UndefinedBehaviorSanitizer only)
#   run-unit_tests.sh --tsan      (ThreadSanitizer)
#
# Exit codes written to /home/daos/test-results/exit_status:
#   sanitizer_detected=1   Set when sanitizer log files are found in sanitizer-logs/
#                          (always 0 for --standard)
#   functional_failure=1   Set when any test recorded a failure in its JUnit/
#                          cmocka XML output (may be 1 at the same time as
#                          sanitizer_detected=1, when a sanitizer finding and an
#                          unrelated plain test failure both occur in one run)
#   runner_error=1         Set when run_utest.py itself crashed/errored out
#                          before finishing (e.g. malformed utest.yaml, missing
#                          build vars) -- an infrastructure-level problem,
#                          distinct from functional_failure, since it isn't
#                          necessarily also caught by another build's run
#
# Outputs mounted back to the host runner:
#   /home/daos/sanitizer-logs/ – per-PID log files (asan.<pid>, ubsan.<pid>, tsan.<pid>)
#   /home/daos/test-results/   – JUnit XML files + exit_status

set -uo pipefail

# ── Parse mode argument ───────────────────────────────────────────────────────
MODE="${1:-}"
if [ "${MODE}" != "--standard" ] && [ "${MODE}" != "--asan" ] \
	&& [ "${MODE}" != "--ubsan" ] && [ "${MODE}" != "--tsan" ]; then
	echo "Usage: $(basename "$0") --standard | --asan | --ubsan | --tsan" >&2
	exit 1
fi

# ── Mode-specific configuration ───────────────────────────────────────────────
RESULTS_DIR=/home/daos/test-results
LOG_DIR=/home/daos/sanitizer-logs
mkdir -p "${RESULTS_DIR}" "${LOG_DIR}"

if [ "${MODE}" = "--standard" ]; then
	# No sanitizer runtime: run every suite not explicitly excluded from GHA
	# (asan:/tsan: tags in utest.yaml only apply when --asan/--ubsan/--tsan
	# is passed to run_utest.py, so passing nothing here selects them all).
	PYTHON_FLAG=""

elif [ "${MODE}" = "--asan" ]; then
	# ASan: detect_odr_violation=0 suppresses the DAOS dual-library false positives.
	# UBSAN_OPTIONS is intentionally NOT set: the UBSan workflow uses a separate
	# image (SANITIZERS=undefined) so log_path is never shared between runtimes.
	export ASAN_OPTIONS="log_path=${LOG_DIR}/asan:exitcode=42:print_summary=1:symbolize=1:detect_odr_violation=0"

	PYTHON_FLAG="--asan"

elif [ "${MODE}" = "--ubsan" ]; then
	# UBSan: halt_on_error=1 makes UBSan stop at the first violation, giving
	# one report per process (consistent with ASan and TSan behavior) and a
	# simpler test-name attribution via the snapshot mechanism.
	# flush=1 ensures the report is fully written before _exit() is called.
	# ASAN_OPTIONS is intentionally NOT set: this image is built with
	# SANITIZERS=undefined only, so there is no ASan runtime present.
	export UBSAN_OPTIONS="log_path=${LOG_DIR}/ubsan:exitcode=42:halt_on_error=1:print_summary=1:print_stacktrace=1:flush=1"

	PYTHON_FLAG="--ubsan"

else
	# TSan: use suppressions file if it exists (for Argobots ULT false positives)
	SUPP_FILE=""
	if [ -f "$(pwd)/daos/utils/test_tsan.supp" ]; then
		SUPP_FILE="$(pwd)/daos/utils/test_tsan.supp"
	fi
	export TSAN_OPTIONS="log_path=${LOG_DIR}/tsan:exitcode=42:second_deadlock_stack=1:print_summary=1${SUPP_FILE:+:suppressions=${SUPP_FILE}}"

	PYTHON_FLAG="--tsan"
fi

# ── Mount tmpfs for PMDK / VOS tests ─────────────────────────────────────────
# Applies to all modes, including --standard: VOS/storage-engine suites are
# I/O heavy and are dramatically slower against a disk-backed directory.
mkdir -p /mnt/daos
mount -t tmpfs \
	-o rw,noatime,inode64,huge=always,mpol=prefer:0,uid="$(id -u)",gid="$(id -g)" \
	tmpfs /mnt/daos

cd daos
# shellcheck source=/dev/null
source utils/sl/setup_local.sh

# ── Run the unit-test suite ───────────────────────────────────────────────────
# --{asan,tsan}     : select suites via asan:/tsan: flags in utest.yaml
# --sudo no         : container already runs as root; no nested sudo needed
# --no-fail-on-error: collect the exit code ourselves so every suite runs
export CMOCKA_XML_FILE="${RESULTS_DIR}/cmocka-%g.xml"
# CMOCKA_XML_FILE alone is not enough: cmocka only writes XML when "xml" is
# one of the active CMOCKA_MESSAGE_OUTPUT formats (defaults to stdout only).
export CMOCKA_MESSAGE_OUTPUT=xml
export PMEMOBJ_CONF="sds.at_create=0"

set +e   # do not abort on test failure; we capture the exit code
# shellcheck disable=SC2086  # PYTHON_FLAG is intentionally empty for --standard
python3 utils/run_utest.py \
	${PYTHON_FLAG} \
	--gha \
	--sudo no \
	--no-fail-on-error \
	--log_dir "${RESULTS_DIR}/logs"
RUNNER_RC=$?
set -e

# ── Detect sanitizer findings and functional-test failures ───────────────────
FUNCTIONAL_FAILURE=0
SANITIZER_DETECTED=0
RUNNER_ERROR=0

if [ "${MODE}" != "--standard" ]; then
	# Derive the log-file prefix from the mode flag to check for findings.
	case "${MODE}" in --asan) PREFIX=asan ;; --ubsan) PREFIX=ubsan ;; *) PREFIX=tsan ;; esac
	ls "${LOG_DIR}/${PREFIX}".* >/dev/null 2>&1 && SANITIZER_DETECTED=1
fi

# run_utest.py is always run with --no-fail-on-error so that every suite still
# runs to completion after a failure; this means RUNNER_RC is 0 whenever
# run_utest.py itself ran to completion, even when individual tests failed --
# it never propagates a test's own exit code (e.g. a sanitizer's exitcode=42)
# as its own (subprocess.run() is called with check=False). So RUNNER_RC can
# never actually be 42; a nonzero RUNNER_RC here only ever means run_utest.py
# crashed/errored out before finishing (e.g. malformed utest.yaml, missing
# build vars). This is an infrastructure-level problem, not a test-content
# failure, so it is tracked separately from FUNCTIONAL_FAILURE below and
# always fails the job regardless of mode: unlike a plain test failure (which
# the standard build's own, separate run would independently catch too), a
# crash specific to one build's container/environment might not be.
if [ "${RUNNER_RC}" -ne 0 ]; then
	RUNNER_ERROR=1
fi

# Individual test failures are recorded in the JUnit/cmocka XML files but (per
# above) never affect RUNNER_RC, so check them directly. This is the same
# check summarize_functional_failures.py uses to build the job summary, kept
# in sync so the job's pass/fail status always matches what the summary shows.
if ! python3 utils/ci/summarize_functional_failures.py --results-dir "${RESULTS_DIR}" >/dev/null; then
	FUNCTIONAL_FAILURE=1
fi

echo "  sanitizer_detected=${SANITIZER_DETECTED}"
echo "  runner_error=${RUNNER_ERROR}"

# ── Write a structured status file readable by the workflow ──────────────────
printf 'sanitizer_detected=%s\nfunctional_failure=%s\nrunner_error=%s\n' \
	"${SANITIZER_DETECTED}" "${FUNCTIONAL_FAILURE}" "${RUNNER_ERROR}" > "${RESULTS_DIR}/exit_status"

echo "=== Test run complete ==="
echo "  mode=${MODE}"
echo "  functional_failure=${FUNCTIONAL_FAILURE}"

# Always exit 0 here so Docker returns control to the workflow, which
# will inspect exit_status and fail the job with a meaningful message.
exit 0
