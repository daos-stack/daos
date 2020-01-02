#!/bin/bash
## Run linters across control plane code and execute Go tests
set -eu

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

	source "${build_source}"

	# allow cgo to find and link to third-party libs
	LD_LIBRARY_PATH=${SL_PREFIX}/lib64
	LD_LIBRARY_PATH+=":${SL_SPDK_PREFIX}/lib"
	LD_LIBRARY_PATH+=":${SL_HWLOC_PREFIX}/lib"
	LD_LIBRARY_PATH+=":${SL_OFI_PREFIX}/lib"
	CGO_LDFLAGS=-L${SL_PREFIX}/lib64
	CGO_LDFLAGS+=" -L${SL_SPDK_PREFIX}/lib"
	CGO_LDFLAGS+=" -L${SL_HWLOC_PREFIX}/lib"
	CGO_LDFLAGS+=" -L${SL_OFI_PREFIX}/lib"
	CGO_CFLAGS=-I${SL_PREFIX}/include
	CGO_CFLAGS+=" -I${SL_SPDK_PREFIX}/include"
	CGO_CFLAGS+=" -I${SL_HWLOC_PREFIX}/include"
	CGO_CFLAGS+=" -I${SL_OFI_PREFIX}/include"
}

function check_formatting()
{
	srcdir=${1:-"./"}
	output=$(find "$srcdir/" -name '*.go' -and -not -path '*vendor*' \
		-print0 | xargs -0 gofmt -d)
	if [ -n "$output" ]; then
		echo "ERROR: Your code hasn't been run through gofmt!"
		echo "Please configure your editor to run gofmt on save."
		echo "Alternatively, at a minimum, run the following command:"
		echo -n "find $srcdir/ -name '*.go' -and -not -path '*vendor*'"
		echo "| xargs gofmt -w"
		echo -e "\ngofmt check found the following:\n\n$output\n"
		exit 1
	fi
}

check=$(check_environment)

if [ "$check" == "false" ]; then
	setup_environment
fi

DIR="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")"
GOPATH="$(readlink -f "$DIR/../../build/src/control")"
repopath=github.com/daos-stack/daos
controldir="$GOPATH/src/$repopath/src/control"

check_formatting "$controldir"

echo "Environment:"
echo "  LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo "  CGO_LDFLAGS: $CGO_LDFLAGS"
echo "  CGO_CFLAGS: $CGO_CFLAGS"

echo "  GOPATH: $GOPATH"
echo

echo "Running all tests under $controldir..."
pushd "$controldir" >/dev/null
set +e
LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
CGO_LDFLAGS="$CGO_LDFLAGS" \
CGO_CFLAGS="$CGO_CFLAGS" \
GOPATH="$GOPATH" \
	go test -race -cover -v ./...
testrc=$?
popd >/dev/null

echo "Tests completed with rc: $testrc"
exit $testrc
