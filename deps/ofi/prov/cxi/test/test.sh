#!/bin/bash
#
# set -x
#
# Run CXI unit tests.
#
# ################################################################
#
# Tests are declared as an array with up to 3 strings:
# 1) the test body
# 2) an optional prolog
# 3) an optional epilog
#
# The strings are executed with the shell 'eval' function.
# They may contain more than one statement separated by semi-colons.
# You probably want to escape your '\$' and '\"' in the strings....
# Prologs and epilogs may be "".  Or absent if both at "".
# Output from test body is captured in $TEST_OUTPUT automatically.
# Output from prologs and epilogs is not captured by default.
#
# Tests are grouped into suites.  Since Bash does not
# really have 2 dimensional arrays, the suites are arrays
# of test names, which match the variable names of the tests.
#
# The long suite is selected by default.
# The short and dummy suites can be selected with -s or -d.
#
# A no-execute mode is selected with -n.  This prints the
# test name, prolog, body and epilog for every test in the
# selected suite.
#
# See default_env for environment variables common to all tests.
# Overriding for a particular test is supported in the test body.
#
# To disable a test, comment out the name in the suite.
# ################################################################
#
# The examples:

dummy_test1=(
	"echo \"dummy test\""
	"echo \"dummy prolog\"; echo \$(hostname)"
	"echo \"dummy epilog\""
)

dummy_test2=(
	"echo \"dummy test with epilog but no prolog\""
	""
	"echo \"dummy epilog\"")

dummy_test3=(
	"echo \"simple dummy test\"")

dummy_test_suite=(
	"dummy_test1"
	"dummy_test2"
	"dummy_test3"
)

# ################################################################
# The short tests and short test suite

short_test1=(
	"./cxitest --verbose --filter=\"@(msg*|tagged*|rma*|atomic*)/*\" -j 1 -f --tap=cxitest-short.tap")

short_test_suite=(
	"short_test1"
)

# ################################################################
# the long tests and long test suite

basic_test=("./cxitest --verbose --tap=cxitest.tap -j 1")

swget_test=(
	"FI_CXI_RGET_TC=BULK_DATA ./cxitest --verbose --filter=\"@(tagged|msg)/*\" --tap=cxitest-swget.tap -j1"
	"csrutil store csr C_LPE_CFG_GET_CTRL get_en=0 > /dev/null"
	"csrutil store csr C_LPE_CFG_GET_CTRL get_en=1 > /dev/null")

swget_unaligned_test=(
	"FI_CXI_RDZV_THRESHOLD=2036 ./cxitest --verbose --filter=\"@(tagged|msg)/*\" --tap=cxitest-swget-unaligned.tap -j1"
	"csrutil store csr C_LPE_CFG_GET_CTRL get_en=0 > /dev/null"
	"csrutil store csr C_LPE_CFG_GET_CTRL get_en=1 > /dev/null")

constrained_le_test=(
	"FI_CXI_DEFAULT_CQ_SIZE=16384 ./cxitest --verbose --filter=\"@(tagged|msg)/fc*\" --tap=cxitest-constrained-le.tap -j1"
	"MAX_ALLOC=\$(csrutil dump csr le_pools[63] | grep max_alloc | awk '{print \$3}'); echo \"Saving MAX_ALLOC=\$MAX_ALLOC\"; csrutil store csr le_pools[] max_alloc=10 > /dev/null"
	"echo \"Restoring MAX_ALLOC=\$MAX_ALLOC\"; csrutil store csr le_pools[] max_alloc=\$MAX_ALLOC > /dev/null")

hw_matching_rendezvous_test=(
	"FI_CXI_DEVICE_NAME=\"cxi1,cxi0\" FI_CXI_RDZV_GET_MIN=0 FI_CXI_RDZV_THRESHOLD=2048 ./cxitest --verbose -j 1 --filter=\"tagged_directed/*\" --tap=cxitest-hw-rdzv-tag-matching.tap")

sw_matching_rendezvous_test=(
	"FI_CXI_RX_MATCH_MODE=\"software\" FI_CXI_RDZV_GET_MIN=0 FI_CXI_RDZV_THRESHOLD=2048 ./cxitest --verbose -j 1 --filter=\"@(tagged|msg)/*\" --tap=cxitest-sw-ep-mode.tap")

fc_eq_space_test=(
	"FI_CXI_DEFAULT_CQ_SIZE=64 FI_CXI_DISABLE_EQ_HUGETLB=1 FI_CXI_RDZV_GET_MIN=0 FI_CXI_RDZV_THRESHOLD=2048 ./cxitest --filter=\"msg/fc_no_eq_space_expected_multi_recv\" --verbose -j 1 --tap=cxitest-fc-eq-space.tap")

fc_eq_20_percent_test=(
	"FI_CXI_CQ_FILL_PERCENT=20 FI_CXI_DEFAULT_CQ_SIZE=64 FI_CXI_DISABLE_EQ_HUGETLB=1 FI_CXI_RDZV_GET_MIN=0 FI_CXI_RDZV_THRESHOLD=2048 ./cxitest --filter=\"msg/fc_no_eq_space_expected_multi_recv\" --verbose -j 1 --tap=cxitest-fc-20%-eq-space.tap")

fi_info_test=(
	"./fi_info_test.sh --tap=fi_info.tap")

unoptimized_mr_test=(
	"FI_CXI_OPTIMIZED_MRS=0 ./cxitest --filter=\"amo_hybrid_mr_desc/*\" -j 1 -f --verbose --tap=cxitest-hybrid_mr_desc_unopt_mrs.tap")

provider_keys_mr_test=(
	"CXIP_TEST_PROV_KEY=1 ./cxitest -j 1 -f --verbose --tap=cxitest-prov_key_mrs.tap")

unoptimized_provider_keys_mr_test=(
	"CXIP_TEST_PROV_KEY=1 FI_CXI_OPTIMIZED_MRS=0 ./cxitest --filter=\"@(rma|mr)/*\" -j 1 -f --verbose --tap=cxitest-prov_key_no_opt_mrs.tap")

provider_keys_std_fallback_test=(
	"CXIP_TEST_PROV_KEY=1 FI_MR_CACHE_MONITOR=\"disabled\" ./cxitest --filter=\"mr_resources/opt_fallback\" -j 1 -f --verbose --tap=cxitest-prov_key_opt_to_std.tap")

zero_eager_size_test=(
	"FI_CXI_RDZV_EAGER_SIZE=0 ./cxitest --filter=\"@(tagged|msg)/*\" -j 1 -f --verbose --tap=cxitest-zero-rdzv-eager-size.tap")

alt_read_rendezvous_test=(
	"FI_CXI_RDZV_PROTO=\"alt_read\" ./cxitest --filter=\"tagged/*rdzv\" -j 1 -f --verbose --tap=cxitest-alt-read-rdzv.tap"
	"csrutil store csr C_LPE_CFG_GET_CTRL get_en=0 > /dev/null"
	"csrutil store csr C_LPE_CFG_GET_CTRL get_en=1 > /dev/null")

mr_mode_no_compat_test=(
	"FI_CXI_COMPAT=0 ./cxitest -j 1 --filter=\"getinfo_infos/*\" -f --verbose --tap=cxitest-mr-mode-no-compat.tap")

mr_mode_with_odp_test=(
	"FI_CXI_ODP=1 ./cxitest -j 1 --filter=\"getinfo_infos/*\" -f --verbose --tap=cxitest-mr-mode-with-odp.tap")

mr_mode_with_prov_keys_odp_test=(
	"FI_CXI_ODP=1 CXIP_TEST_PROV_KEY=1 ./cxitest -j 1 --filter=\"getinfo_infos/*\" -f --verbose --tap=cxitest-mr-mode-with-prov-key-odp.tap")

cxi_fork_safe_test=(
	"CXI_FORK_SAFE=1 CXI_FORK_SAFE_HP=1 ./cxitest --verbose --tap=cxitest-fork-safe.tap --filter=\"@(rma*|tagged*|msg*|atomic*)/*\" -j 1")

fork_safe_monitor_disabled_test=(
	"FI_MR_CACHE_MONITOR=\"disabled\" ./cxitest --verbose --tap=cxitest-fork_tests-mr_cache_disabled.tap --filter=\"fork/*\" -j 1")

fork_safe_uffd_test=(
	"FI_MR_CACHE_MONITOR=\"uffd\" ./cxitest --verbose --tap=cxitest-fork_tests-mr_cache_uffd.tap --filter=\"fork/*\" -j 1")

fork_safe_memhooks_test=(
	"FI_MR_CACHE_MONITOR=\"memhooks\" ./cxitest --verbose --tap=cxitest-fork_tests-mr_cache_memhooks.tap --filter=\"fork/*\" -j 1")

fork_safe_kdreg2_test=(
	"FI_MR_CACHE_MONITOR=\"kdreg2\" ./cxitest --verbose --tap=cxitest-fork_tests-mr_cache_kdreg2.tap --filter=\"fork/*\" -j 1")

unlimited_triggered_ops_test=(
	"FI_CXI_ENABLE_TRIG_OP_LIMIT=0 ./cxitest -j 1 --verbose --filter=\"deferred_work_trig_op_limit/*\" --tap=cxitest-disable-trig-op-limit.tap")

long_test_suite=(
	"basic_test"
	"swget_test"
	"swget_unaligned_test"
	"constrained_le_test"
	"hw_matching_rendezvous_test"
	"sw_matching_rendezvous_test"
	"fc_eq_space_test"
	"fc_eq_20_percent_test"
	"fi_info_test"
	"unoptimized_mr_test"
	"provider_keys_mr_test"
	"unoptimized_provider_keys_mr_test"
	"provider_keys_std_fallback_test"
	"zero_eager_size_test"
	"alt_read_rendezvous_test"
	"mr_mode_no_compat_test"
	"mr_mode_with_odp_test"
	"mr_mode_with_prov_keys_odp_test"
	"cxi_fork_safe_test"
	"fork_safe_monitor_disabled_test"
	"fork_safe_uffd_test"
	"fork_safe_memhooks_test"
	"fork_safe_kdreg2_test"
	"unlimited_triggered_ops_test"
)

# ################################################################

known_suites=(
	"short_test_suite"
	"long_test_suite"
	"dummy_test_suite"
)

default_test_suite="long_test_suite"

# ################################################################

default_env=(
	"DMA_FAULT_RATE=0.1"
	"MALLOC_FAULT_RATE=0.1"
	"FI_LOG_LEVEL=warn"
	"FI_CXI_FC_RECOVERY=1"
	"FI_CXI_ENABLE_TRIG_OP_LIMIT=1"
	"FI_MR_CACHE_MONITOR=uffd"
)

# ################################################################

dashes="----------------------------------------------------------------"

# ################################################################

print_suites() {

	for suite in "${known_suites[@]}"; do
		echo "Suite: $suite"
		local -n tests="$suite"
		for test in "${tests[@]}"; do
			echo "    $test"
		done;
	done;

	return 0
}

# ################################################################
# Function to run one test
# It expects the following argument:
#   test name

run_one_test() {
	if [ $# -eq 0 ]; then
		echo "$0 called with no arguments (?)"
		exit 1
	fi
	local name="$1"

	local -n elements="$name"
	local -i num_elements=${#elements[@]}

	if [ $no_execute -ne 0 ]; then
		echo $dashes
	fi

	if [ $num_elements -lt 1 ]; then
		echo "Test $name not found"
		return 1
	elif [ $num_elements -gt 3 ]; then
		echo "test $1 malformed: maximum 3 elements in array: test prolog epilog"
		exit 1
	fi

	local test_body="${elements[0]}"
	if [ $num_elements -ge 2 ]; then
		local prolog="${elements[1]}"
	else
		local prolog=""
	fi
	if [ $num_elements -ge 3 ]; then
		local epilog="${elements[2]}"
	else
		local epilog=""
	fi

	local full_test_string="$test_body >> $TEST_OUTPUT 2>&1"

	if [ $no_execute -ne 0 ]; then
		echo "Test name: $name"
		echo "Prolog: $prolog"
		echo "Test body: $full_test_string"
		echo "Epilog: $epilog"
		return 0
	fi

	if [ -n "$prolog" ]; then
		echo "Running $name prolog: $prolog"
		eval $prolog
	fi

	echo "Running $name: $full_test_string" | tee -a $TEST_OUTPUT
	eval $full_test_string
	local -i test_result=$?

	if [ $test_result -ne 0 ]; then
		echo "Test $name returns non-zero exit code. Possible failures in test teardown."
	fi

	if [ -n "$epilog" ]; then
		echo "Running $name epilog: $epilog"
		eval $epilog
	fi

	return $test_result
}

# ################################################################
# Function to run a list of tests

run_tests() {
	local ret=0
	for test in $@; do
		run_one_test "$test"
		local r=$?
		if [ $r -ne 0 ]; then
			ret=$r
			if [ $fail_fast -ne 0 ]; then
				break
			fi
		fi
	done
	return $ret
}

# ################################################################
# Function to run all the tests in a suite
# It expects the following argument:
#   suite name

run_test_suite() {
	if [ $# -ne 1 ]; then
		echo "$0 called with no arguments (?)"
		exit 1
	fi
	local suite=$1

	echo "Running Suite: $suite"

	local -n tests=$suite

	run_tests "${tests[@]}"
	local ret=$?

	if [ $no_execute -ne 0 ]; then
		echo $dashes
	fi

	return $ret
}

# ################################################################

print_help() {
cat <<EOF
Usage: $SCRIPT [-sdpnefh] -t test1 [test2 ...]

With no options, $SCRIPT runs $default_test_suite.

  -s: run the short_test_suite
  -d: run the dummy_test_suite
  -p: print the known test suites and test names
  -n: print (but do not execute) the commands for a test suite
  -e: print the environment variables when starting test run
  -f: fail fast (stop executing after first test failure)
  -h: print the usage information

  -t test1 [test2 ...]: execute the tests listed on the command line

  (the -s -d and -t options are mutually exclusive)

EOF
}

# ################################################################

DIR=$(dirname ${BASH_SOURCE[0]:-$0})
SCRIPT=$(basename ${BASH_SOURCE[0]:-$0})
TEST_OUTPUT=cxitest.out

suite=$default_test_suite
test_names=()

declare -i no_execute=0
declare -i show_environment=0
declare -i run_specific_tests=0
declare -i fail_fast=0
declare -i exclusive=0

while getopts "spdnefht:" option; do
	case "${option}" in
		s)
			suite="short_test_suite"
			exclusive=$((exclusive + 1))
			;;
		d)
			suite="dummy_test_suite"
			exclusive=$((exclusive + 1))
			;;
		p)
			print_suites
			exit 0
			;;
		n)
			no_execute=1
			;;
		e)
			show_environment=1
			;;
		f)
			fail_fast=1
			;;
		t)
			run_specific_tests=1
			test_names+=("$OPTARG")
			exclusive=$((exclusive + 1))
			;;
		h)
			print_help
			exit 0
			;;
		*)
			exit 1
			;;
	esac
done

if [ $exclusive -gt 1 ]; then
	echo "$SCRIPT: Please specify only one of -s -d or -t."
	exit 0
fi

# get the rest of the command line if -t was given
if [ $run_specific_tests -ne 0 ]; then
	shift $(expr $OPTIND - 1 )
	test_names+=($@)
fi

# setup the environment
for var in "${default_env[@]}"; do
	export $var
done

# Run unit tests. $(CWD) should be writeable.

cd $DIR

echo "Clearing output file: $(realpath $TEST_OUTPUT)"

rm -f $TEST_OUTPUT
touch $TEST_OUTPUT

if [ $show_environment -ne 0 ]; then
	echo "Initial test environment variables:" | tee -a $TEST_OUTPUT
	eval "printenv" | tee -a $TEST_OUTPUT
fi

if [ $run_specific_tests -ne 0 ]; then
	run_tests "${test_names[@]}"
else
	run_test_suite $suite
fi
ret=$?

grep "Tested" $TEST_OUTPUT

echo "$SCRIPT exits, return code $ret"

exit $ret
