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

	# ugh, appease the linter...
	LD_LIBRARY_PATH=${SL_PREFIX}/lib
	LD_LIBRARY_PATH+=":${SL_SPDK_PREFIX}/lib"
	LD_LIBRARY_PATH+=":${SL_HWLOC_PREFIX}/lib"
	CGO_LDFLAGS=-L${SL_PREFIX}/lib
	CGO_LDFLAGS+=" -L${SL_SPDK_PREFIX}/lib"
	CGO_LDFLAGS+=" -L${SL_HWLOC_PREFIX}/lib"
	CGO_CFLAGS=-I${SL_PREFIX}/include
	CGO_CFLAGS+=" -I${SL_SPDK_PREFIX}/include"
	CGO_CFLAGS+=" -I${SL_HWLOC_PREFIX}/include"
}

check=$(check_environment)

if [ "$check" == "false" ]; then
	setup_environment
fi

echo "Environment:"
echo "  LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo "  CGO_LDFLAGS: $CGO_LDFLAGS"
echo "  CGO_CFLAGS: $CGO_CFLAGS"

DIR="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")"

GOPATH="$(readlink -f "$DIR/../../build/src/control")"
echo "  GOPATH: $GOPATH"
echo

repopath=github.com/daos-stack/daos
controldir="$GOPATH/src/$repopath/src/control"

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
