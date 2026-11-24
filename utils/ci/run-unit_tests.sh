#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Copyright 2025-2026 Hewlett Packard Enterprise Development LP.
#
# Run the DAOS unit tests, optionally under a sanitizer, and collect all the reports. Shared by the
# standard, ASan, UBSan and TSan GHA jobs and by utils/docker/run-unit_tests-docker.sh.

set -euo pipefail

PROG="${0##*/}"
CWD="$(realpath "$0")"
CWD="${CWD%/*}"

RESULTS_DIR=/home/daos/test-results
LOG_DIR=/home/daos/sanitizer-logs

function usage
{
	cat <<- EOF
		usage: ${PROG} --standard|--asan|--ubsan|--tsan [OPTIONS]

		Run the DAOS unit tests, optionally under a sanitizer, and collect all the reports.
		It mounts a tmpfs on /mnt/daos: run it as root in a privileged container.

		Modes, exactly one is required:
		   --standard               run the suites without sanitizer
		   --asan                   run the suites under ASan, and LSan for the leaks
		   --ubsan                  run the suites under UBSan
		   --tsan                   run the suites under TSan

		Options:
		   --list                   list the selected suites and tests, without running them
		   --suite-filter=REGEX     only run the suites whose name matches REGEX
		   --test-filter=REGEX      only run the tests whose command line matches REGEX
		   -h, --help               show this help message and exit

		Environment:
		   ASAN_OPTIONS, LSAN_OPTIONS, UBSAN_OPTIONS, TSAN_OPTIONS
		       replace the default options of the sanitizer, e.g. to add
		       fast_unwind_on_malloc=1 for a deeper leak trace (see docs/dev/development.md)
		   ASAN_EXTRA_OPTIONS, LSAN_EXTRA_OPTIONS, UBSAN_EXTRA_OPTIONS, TSAN_EXTRA_OPTIONS
		       append options to the ones in effect, default or overridden

		Results:
		   /home/daos/test-results/
		       JUnit and cmocka XML files
		   /home/daos/sanitizer-logs/
		       sanitizer logs, one file per process: asan.<pid>, ubsan.<pid> or tsan.<pid>
		   /home/daos/test-results/exit_status
		       status read by the workflow, with 0 or 1 for each of these keys:
		       sanitizer_detected  a sanitizer reported a finding (0 with --standard)
		       functional_failure  a test failed, according to its XML files
		       runner_error        run_utest.py crashed, e.g. on a malformed utest.yaml

		test-results/ and sanitizer-logs/ are emptied when the tests start, except with
		--list, so that they only hold the results of this run.

		The exit status is 0 once the tests ran, whatever their results.
	EOF
}

function fatal
{
	echo "${PROG}: $*" >&2
	echo "Try '${PROG} --help' for more information." >&2
	exit 1
}

# Skip the empty fragments, e.g. an unset *_EXTRA_OPTIONS, to avoid stray colons
function join_options
{
	local out=""
	for frag in "$@"; do
		[ -z "${frag}" ] && continue
		if [ -z "${out}" ]; then out="${frag}"; else out="${out}:${frag}"; fi
	done
	printf '%s' "${out}"
}

OPTIONS=$(getopt -n "${PROG}" -o h \
	--long help,standard,asan,ubsan,tsan,list,suite-filter:,test-filter: -- "$@") \
	|| fatal "invalid command line"
eval set -- "${OPTIONS}"

MODE=""
LIST_MODE=0
EXTRA_ARGS=()
while true; do
	case "$1" in
		-h|--help) usage; exit 0 ;;
		--standard|--asan|--ubsan|--tsan)
			[ -z "${MODE}" ] || fatal "only one mode can be used"
			MODE="$1"; shift ;;
		--list) LIST_MODE=1; EXTRA_ARGS+=("$1"); shift ;;
		--suite-filter|--test-filter) EXTRA_ARGS+=("$1=$2"); shift 2 ;;
		--) shift; break ;;
		*) fatal "unrecognized command line option" ;;
	esac
done
[ $# -eq 0 ] || fatal "unexpected argument: $1"
[ -n "${MODE}" ] || fatal "a mode is required: --standard, --asan, --ubsan or --tsan"

mkdir -p "${RESULTS_DIR}" "${LOG_DIR}"

# The verdicts are read from these directories: the files of a previous run would be reported as
# findings of this one. Empty them rather than remove them, they are usually mount points.
# --list runs nothing, so it keeps the previous results.
if [ "${LIST_MODE}" -eq 0 ] && ! find "${RESULTS_DIR}" "${LOG_DIR}" -mindepth 1 -delete; then
	echo "${PROG}: cannot empty ${RESULTS_DIR} and ${LOG_DIR}" >&2
	exit 1
fi

# Each sanitizer job has its own image with only that runtime: only set its options
if [ "${MODE}" = "--standard" ]; then
	PYTHON_FLAG=""

elif [ "${MODE}" = "--asan" ]; then
	# - exitcode=42: exit code used by sanitizers to signal a finding
	# - symbolize=1: print the function names and line numbers instead of raw addresses
	# - print_summary=1: print a summary of the findings at the end of the log file
	# - detect_odr_violation=1: skip the same-size duplicates of globals between DAOS libraries
	#   (DAOS-19023), report only those of different sizes. Reset to 2 once it is fixed
	if [ -z "${ASAN_OPTIONS+set}" ]; then
		ASAN_OPTIONS=$(join_options "log_path=${LOG_DIR}/asan" "exitcode=42" \
			"print_summary=1" "symbolize=1" "detect_odr_violation=1")
	fi
	ASAN_OPTIONS=$(join_options "${ASAN_OPTIONS}" "${ASAN_EXTRA_OPTIONS:-}")
	export ASAN_OPTIONS

	# test_lsan.supp suppresses the leaks of third-party code that are not DAOS bugs
	LSAN_SUPP_FILE=""
	if [ -f "${CWD}/../test_lsan.supp" ]; then
		LSAN_SUPP_FILE="${CWD}/../test_lsan.supp"
	fi
	if [ -z "${LSAN_OPTIONS+set}" ]; then
		LSAN_OPTIONS=$(join_options "${LSAN_SUPP_FILE:+suppressions=${LSAN_SUPP_FILE}}")
	fi
	LSAN_OPTIONS=$(join_options "${LSAN_OPTIONS}" "${LSAN_EXTRA_OPTIONS:-}")
	export LSAN_OPTIONS

	PYTHON_FLAG="--asan"

elif [ "${MODE}" = "--ubsan" ]; then
	# - exitcode=42: exit code used by sanitizers to signal a finding
	# - halt_on_error=1: stop at the first finding, like ASan, so a log holds a single report
	# - print_summary=1: end the log with a summary line (UBSan does not print one by default)
	# - print_stacktrace=1: print the stack trace of the finding, like ASan and TSan
	if [ -z "${UBSAN_OPTIONS+set}" ]; then
		UBSAN_OPTIONS=$(join_options "log_path=${LOG_DIR}/ubsan" "exitcode=42" \
			"halt_on_error=1" "print_summary=1" "print_stacktrace=1")
	fi
	UBSAN_OPTIONS=$(join_options "${UBSAN_OPTIONS}" "${UBSAN_EXTRA_OPTIONS:-}")
	export UBSAN_OPTIONS

	PYTHON_FLAG="--ubsan"

else
	# test_tsan.supp suppresses the Argobots ULT false positives and the confirmed-benign races
	TSAN_SUPP_FILE=""
	if [ -f "${CWD}/../test_tsan.supp" ]; then
		TSAN_SUPP_FILE="${CWD}/../test_tsan.supp"
	fi
	# - exitcode=42: exit code used by sanitizers to signal a finding
	# - second_deadlock_stack=1: report where each mutex is locked in a deadlock report
	# - print_summary=1: print a summary of the findings at the end of the log file
	if [ -z "${TSAN_OPTIONS+set}" ]; then
		TSAN_OPTIONS=$(join_options "log_path=${LOG_DIR}/tsan" "exitcode=42" \
			"second_deadlock_stack=1" "print_summary=1" \
			"${TSAN_SUPP_FILE:+suppressions=${TSAN_SUPP_FILE}}")
	fi
	TSAN_OPTIONS=$(join_options "${TSAN_OPTIONS}" "${TSAN_EXTRA_OPTIONS:-}")
	export TSAN_OPTIONS

	# UCX's memory-event hooks are not TSan-safe: they can crash with a plain SIGSEGV, without
	# any TSan report, so sanitizer_detected would stay 0 and the job would pass. Disabling them
	# avoids the crash and does not affect the detection of the real data races.
	if [ -z "${UCX_MEM_EVENTS+set}" ]; then
		export UCX_MEM_EVENTS=n
	fi

	PYTHON_FLAG="--tsan"
fi

# The VOS suites are I/O heavy, and much slower on a disk-backed directory.
# --list runs nothing: it does not need the mount, so it also works without privileges.
if [ "${LIST_MODE}" -eq 0 ]; then
	mkdir -p /mnt/daos
	mount -t tmpfs \
		-o rw,noatime,inode64,huge=always,mpol=prefer:0,uid="$(id -u)",gid="$(id -g)" \
		tmpfs /mnt/daos
fi

# setup_local.sh looks for .build_vars.sh in the current directory
cd "${CWD}/../.."
# shellcheck source=/dev/null
source "${CWD}/../sl/setup_local.sh"

export CMOCKA_XML_FILE="${RESULTS_DIR}/cmocka-%g.xml"
# cmocka only writes the XML file when xml is one of its message output formats
export CMOCKA_MESSAGE_OUTPUT=xml
export PMEMOBJ_CONF="sds.at_create=0"

# - --gha: only run the suites tagged for GitHub Actions, and group the log by suite
# - --sudo no: skip VOS_NVMe and VOS_md_on_ssd, as SPDK needs hugepages, which the container lacks.
#   Jenkins runs them in its "Unit Test bdev" stage
# - --no-fail-on-error: exit 0 when a test fails, the failures are collected below
# A failing runner must not stop the script: its status is reported as runner_error below
set +e
# shellcheck disable=SC2086  # PYTHON_FLAG is intentionally empty for --standard
python3 "${CWD}/../run_utest.py" \
	${PYTHON_FLAG} \
	--gha \
	--sudo no \
	--no-fail-on-error \
	--log_dir "${RESULTS_DIR}/logs" \
	"${EXTRA_ARGS[@]}"
RUNNER_RC=$?
set -e

# Nothing ran, so there is nothing to report
if [ "${LIST_MODE}" -eq 1 ]; then
	exit "${RUNNER_RC}"
fi

FUNCTIONAL_FAILURE=0
SANITIZER_DETECTED=0
RUNNER_ERROR=0

if [ "${MODE}" != "--standard" ]; then
	case "${MODE}" in --asan) PREFIX=asan ;; --ubsan) PREFIX=ubsan ;; *) PREFIX=tsan ;; esac
	if [ "${MODE}" = "--ubsan" ]; then
		# A suppressed finding leaves no UBSan log file, so any log file is a finding
		ls "${LOG_DIR}/${PREFIX}".* >/dev/null 2>&1 && SANITIZER_DETECTED=1
	else
		# A fully suppressed run can still leave a log file, e.g. LSan writes a
		# "Suppressions used:" footer: look for the report marker instead of the file
		case "${MODE}" in
			--asan) MARKER='^==[0-9]+==ERROR:' ;;
			*)      MARKER='^WARNING: ThreadSanitizer:' ;;
		esac
		if ls "${LOG_DIR}/${PREFIX}".* >/dev/null 2>&1 \
			&& grep -E -q "${MARKER}" "${LOG_DIR}/${PREFIX}".* 2>/dev/null; then
			SANITIZER_DETECTED=1
		fi
	fi
fi

# With --no-fail-on-error, a nonzero status means that run_utest.py itself crashed (e.g. malformed
# utest.yaml). The crash may be specific to this build: report it apart from the test failures.
if [ "${RUNNER_RC}" -ne 0 ]; then
	RUNNER_ERROR=1
fi

# Test failures do not reach RUNNER_RC: use the script that builds the job summary to agree with it
if ! python3 "${CWD}/summarize_functional_failures.py" \
	--results-dir "${RESULTS_DIR}" >/dev/null; then
	FUNCTIONAL_FAILURE=1
fi

echo "  sanitizer_detected=${SANITIZER_DETECTED}"
echo "  runner_error=${RUNNER_ERROR}"

printf 'sanitizer_detected=%s\nfunctional_failure=%s\nrunner_error=%s\n' \
	"${SANITIZER_DETECTED}" "${FUNCTIONAL_FAILURE}" "${RUNNER_ERROR}" \
	> "${RESULTS_DIR}/exit_status"

echo "=== Test run complete ==="
echo "  mode=${MODE}"
echo "  functional_failure=${FUNCTIONAL_FAILURE}"

# Always exit 0 so that the workflow gets control back and fails the job from exit_status
exit 0
