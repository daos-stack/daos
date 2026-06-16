#!/bin/bash
## Run linters across control plane code and execute Go tests
set -eu

function usage()
{
	cat <<EOF
Usage: $(basename "$0") [OPTIONS] [EXTRA_ARGS]

Run Go linters and tests for the DAOS control plane.

Options:
  --dlv <package>   Launch an interactive Delve (dlv) debug session for the
                    given package instead of running the full test suite.
                    <package> must be a single fully-qualified Go package path.
                    Example: github.com/daos-stack/daos/src/control/fault
  --run <TestName>  Filter the test to run (only valid with --dlv).
                    Passed as -test.run to the binary.
  -h, --help        Show this help message and exit.

Without --dlv, all remaining arguments are forwarded to the test runner
(go test or gotestsum).

Examples:
  # Run all tests
  $(basename "$0")

  # Debug a specific test with dlv
  $(basename "$0") --dlv --run TestFaultComparison \\
      github.com/daos-stack/daos/src/control/fault
EOF
}

function get_cpu_perf()
{
	test_file=$(mktemp primebenchXXX.go)

	cat <<EndOfScript>"$test_file"
package main

import (
	"fmt"
	"math"
	"time"
)

func main() {
	primeCh := make(chan int)
	primes := 0

	go func() {
		isPrime := func(v int) bool {
			for i := 2; i <= int(math.Floor(math.Sqrt(float64(v)))); i++ {
				if v%i == 0 {
					return false
				}
			}
			return v > 1
		}
		for i := 2; ; i++ {
			if isPrime(i) {
				primeCh <- i
			}
		}
	}()

	timeout := 1 * time.Second
	timer := time.NewTimer(timeout)
	for {
		select {
		case <-timer.C:
			fmt.Printf("%d primes in %s", primes, timeout)
			return
		case <-primeCh:
			primes++
		}
	}
}
EndOfScript

	go run "$test_file"
	rm -f "$test_file"
}

function find_build_source()
{
	BASE=$PWD
	while true
	do
		path=$(realpath "${BASE}")
		if [ "${path}" == "/" ]; then
			break
		fi
		bvp="${path}/.build_vars.sh"
		test -e "${bvp}" && echo "${bvp}" && return
		BASE=$(dirname "${path}")
	done
	echo ""
}

function setup_environment()
{
	build_source=$(find_build_source)

	if [ "${build_source}" == "" ]; then
		echo "Unable to find .build_vars.sh" && exit 1
	fi

	# shellcheck disable=SC1090
	source "${build_source}"

	# allow cgo to find and link to third-party libs
	LD_LIBRARY_PATH=${SL_PREFIX+${SL_PREFIX}/lib}
	LD_LIBRARY_PATH+="${SL_PREFIX+:${SL_PREFIX}/lib64}"
	LD_LIBRARY_PATH+="${SL_PREFIX+:${SL_PREFIX}/lib64/daos_srv}"
	LD_LIBRARY_PATH+="${SL_MERCURY_PREFIX+:${SL_MERCURY_PREFIX}/lib}"
	LD_LIBRARY_PATH+="${SL_SPDK_PREFIX+:${SL_SPDK_PREFIX}/lib64/daos_srv}"
	[[ -z "${SL_SPDK_PREFIX:-}" ]] && LD_LIBRARY_PATH+=":/usr/lib64/daos_srv"
	LD_LIBRARY_PATH+="${SL_OFI_PREFIX+:${SL_OFI_PREFIX}/lib}"
	CGO_LDFLAGS=${SL_PREFIX+-L${SL_PREFIX}/lib}
	CGO_LDFLAGS+="${SL_PREFIX+ -L${SL_PREFIX}/lib64}"
	CGO_LDFLAGS+="${SL_PREFIX+ -L${SL_PREFIX}/lib64/daos_srv}"
	CGO_LDFLAGS+="${SL_BUILD_DIR+ -L${SL_BUILD_DIR}/src/control/lib/spdk}"
	CGO_LDFLAGS+="${SL_MERCURY_PREFIX+ -L${SL_MERCURY_PREFIX}/lib}"
	CGO_LDFLAGS+="${SL_SPDK_PREFIX+ -L${SL_SPDK_PREFIX}/lib64/daos_srv}"
	[[ -z "${SL_SPDK_PREFIX:-}" ]] && CGO_LDFLAGS+=" -L /usr/lib64/daos_srv"
	CGO_LDFLAGS+="${SL_OFI_PREFIX+ -L${SL_OFI_PREFIX}/lib}"
	CGO_CFLAGS=${SL_PREFIX+-I${SL_PREFIX}/include}
	CGO_CFLAGS+="${SL_MERCURY_PREFIX+ -I${SL_MERCURY_PREFIX}/include}"
	CGO_CFLAGS+="${SL_SPDK_PREFIX+ -I${SL_SPDK_PREFIX}/include/daos_srv}"
	[[ -z "${SL_SPDK_PREFIX:-}" ]] && CGO_CFLAGS+=" -I /usr/include/daos_srv"
	CGO_CFLAGS+="${SL_OFI_PREFIX+ -I${SL_OFI_PREFIX}/include}"
	CGO_CFLAGS+="${SL_ARGOBOTS_PREFIX+ -I${SL_ARGOBOTS_PREFIX}/include}"

	src_include="$(dirname "$build_source")/src/include"
	if [ -d "$src_include" ]; then
		echo "including path \"${src_include}\" in CGO_CFLAGS"
		CGO_CFLAGS+=" -I${src_include}"
	fi
	export CGO_CFLAGS LD_LIBRARY_PATH CGO_LDFLAGS
}

function emit_junit_failure()
{
    local cname="run_go_tests"
    local tname="${1:-subtest}"
    local fname="${DAOS_BASE}/test_results/${cname}.${tname}.xml"

    local teststr="    <testcase classname=\"$cname\" name=\"$tname\">
    <failure type=\"format\">
      <![CDATA[$2
        ]]>
    </failure>
    </testcase>"

    cat > "${fname}" << EOF
<?xml version="1.0" encoding="UTF-8" ?>
<testsuites>
  <testsuite tests="1" failures="1" errors="0" skipped="0" >
    ${teststr}
  </testsuite>
</testsuites>
EOF
}

function check_formatting()
{
	srcdir=${1:-"./"}
	output=$(find "$srcdir/" -name '*.go' -and -not -path '*vendor*' \
		-and -not -name '*.pb.go' \
		-print0 | xargs -0 gofmt -d)
	if [ -n "$output" ]; then
		errmsg="ERROR: Your code hasn't been run through gofmt!
Please configure your editor to run gofmt on save.
Alternatively, at a minimum, run the following command:
find $srcdir/ -name '*.go' -and -not -path '*vendor*' | xargs gofmt -w

gofmt check found the following:

$output
"
		emit_junit_failure "gofmt" "$errmsg"
		echo "$errmsg"
		exit 1
	fi
}

function get_test_runner()
{
	test_args="-mod vendor -race -cover -v ./... -tags firmware,fault_injection,test_stubs,spdk"
	test_runner="go test"

	if which gotestsum >/dev/null; then
		mkdir -p "$(dirname "$GO_TEST_XML")"
		test_runner="gotestsum --format short "
		test_runner+="--junitfile-testcase-classname relative "
		test_runner+="--junitfile-testsuite-name relative "
		if ${IS_CI:-false}; then
			test_runner+="--no-color "
		fi
		test_runner+="--junitfile $GO_TEST_XML --"
	fi

	echo "$test_runner $test_args"
}

# Parse script-level flags; remaining args are passed through to the test runner
DLV_MODE=false
DLV_TEST_NAME=""
DLV_PACKAGE=""
PASSTHROUGH_ARGS=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--dlv)       DLV_MODE=true; shift ;;
		--run)       DLV_TEST_NAME="$2"; shift 2 ;;
		-h|--help)   usage; exit 0 ;;
		*)           PASSTHROUGH_ARGS+=("$1"); shift ;;
	esac
done
# In dlv mode the first non-flag argument is the package to debug
if $DLV_MODE && [[ ${#PASSTHROUGH_ARGS[@]} -gt 0 ]]; then
	DLV_PACKAGE="${PASSTHROUGH_ARGS[0]}"
fi

setup_environment

DAOS_BASE=${DAOS_BASE:-${SL_SRC_DIR}}

# Allow use of an official toolchain that is newer than
# than the distro-provided toolchain, if necessary.
export GOTOOLCHAIN=${GOTOOLCHAIN:-"auto"}
export GOSUMDB=${GOSUMDB:-"sum.golang.org"}
export GOPROXY=${GOPROXY:-"https://proxy.golang.org,direct"}

export PATH=$SL_PREFIX/bin:$PATH
GO_TEST_XML="$DAOS_BASE/test_results/run_go_tests.xml"
GO_TEST_EXTRA_ARGS="${PASSTHROUGH_ARGS[*]:-}"

controldir="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")"

if $DLV_MODE; then
	if [[ -z "$DLV_PACKAGE" ]]; then
		echo "Usage: $0 --dlv [--run <TestName>] <package>" >&2
		exit 1
	fi
	DLV_BUILD_FLAGS="-mod vendor -tags firmware,fault_injection,test_stubs,spdk"
	DLV_ARGS=()
	[[ -n "$DLV_TEST_NAME" ]] && DLV_ARGS=(-- -test.run "$DLV_TEST_NAME")
	echo "Environment:"
	echo "  GO VERSION: $(go version | awk '{print $3" "$4}')"
	echo "  LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
	echo "  CGO_LDFLAGS: $CGO_LDFLAGS"
	echo "  CGO_CFLAGS: $CGO_CFLAGS"
	echo
	echo "Starting dlv session for $DLV_PACKAGE..."
	pushd "$controldir" >/dev/null
	LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
	CGO_LDFLAGS="$CGO_LDFLAGS" \
	CGO_CFLAGS="$CGO_CFLAGS" \
		dlv test --build-flags "$DLV_BUILD_FLAGS" "$DLV_PACKAGE" "${DLV_ARGS[@]}"
	testrc=$?
	popd >/dev/null
	exit $testrc
fi

GO_TEST_RUNNER=$(get_test_runner)

check_formatting "$controldir"

echo "Environment:"
echo "  GO VERSION: $(go version | awk '{print $3" "$4}')"
echo "  LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo "  CGO_LDFLAGS: $CGO_LDFLAGS"
echo "  CGO_CFLAGS: $CGO_CFLAGS"
echo "  CPU Perf: $(get_cpu_perf)"
echo

echo "Running all tests under $controldir..."
pushd "$controldir" >/dev/null
set +e
LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
CGO_LDFLAGS="$CGO_LDFLAGS" \
CGO_CFLAGS="$CGO_CFLAGS" \
	$GO_TEST_RUNNER "$GO_TEST_EXTRA_ARGS"
testrc=$?
popd >/dev/null

if [ -f "$GO_TEST_XML" ]; then
	# add a newline to make the XML parser happy
	echo >> "$GO_TEST_XML"
fi

echo "Tests completed with rc: $testrc"
exit $testrc
