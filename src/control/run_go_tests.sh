#!/bin/bash
## Run linters across control plane code and execute Go tests
set -eu

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

function check_environment()
{
	if [ -z "${LD_LIBRARY_PATH:-}" ]; then
		echo "false" && return
	fi

	if [ -z "${CGO_LDFLAGS:-}" ]; then
		echo "false" && return
	fi

	if [ -z "${CGO_CFLAGS:-}" ]; then
		echo "false" && return
	fi
	echo "true" && return
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
	LD_LIBRARY_PATH+="${SL_SPDK_PREFIX+:${SL_SPDK_PREFIX}/lib}"
	LD_LIBRARY_PATH+="${SL_OFI_PREFIX+:${SL_OFI_PREFIX}/lib}"
	CGO_LDFLAGS=${SL_PREFIX+-L${SL_PREFIX}/lib}
	CGO_LDFLAGS+="${SL_PREFIX+ -L${SL_PREFIX}/lib64}"
	CGO_LDFLAGS+="${SL_PREFIX+ -L${SL_PREFIX}/lib64/daos_srv}"
	CGO_LDFLAGS+="${SL_BUILD_DIR+ -L${SL_BUILD_DIR}/src/control/lib/spdk}"
	CGO_LDFLAGS+="${SL_MERCURY_PREFIX+ -L${SL_MERCURY_PREFIX}/lib}"
	CGO_LDFLAGS+="${SL_SPDK_PREFIX+ -L${SL_SPDK_PREFIX}/lib}"
	CGO_LDFLAGS+="${SL_OFI_PREFIX+ -L${SL_OFI_PREFIX}/lib}"
	CGO_CFLAGS=${SL_PREFIX+-I${SL_PREFIX}/include}
	CGO_CFLAGS+="${SL_MERCURY_PREFIX+ -I${SL_MERCURY_PREFIX}/include}"
	CGO_CFLAGS+="${SL_SPDK_PREFIX+ -I${SL_SPDK_PREFIX}/include}"
	CGO_CFLAGS+="${SL_OFI_PREFIX+ -I${SL_OFI_PREFIX}/include}"
	CGO_CFLAGS+="${SL_ARGOBOTS_PREFIX+ -I${SL_ARGOBOTS_PREFIX}/include}"

	src_include="$(dirname "$build_source")/src/include"
	if [ -d "$src_include" ]; then
		echo "including path \"${src_include}\" in CGO_CFLAGS"
		CGO_CFLAGS+=" -I${src_include}"
	fi
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
	test_args="-mod vendor -race -cover -v ./... -tags firmware,fault_injection,test_stubs"
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

check=$(check_environment)

if [ "$check" == "false" ]; then
	setup_environment
fi

DAOS_BASE=${DAOS_BASE:-${SL_SRC_DIR}}

# Allow use of an official toolchain that is newer than
# than the distro-provided toolchain, if necessary.
export GOTOOLCHAIN=${GOTOOLCHAIN:-"auto"}
export GOSUMDB=${GOSUMDB:-"sum.golang.org"}
export GOPROXY=${GOPROXY:-"https://proxy.golang.org,direct"}

export PATH=$SL_PREFIX/bin:$PATH
GO_TEST_XML="$DAOS_BASE/test_results/run_go_tests.xml"
GO_TEST_RUNNER=$(get_test_runner)
GO_TEST_EXTRA_ARGS=${*:-""}

controldir="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")"

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
