#!/bin/bash
## Run linters across control plane code and execute Go tests
set -e

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
	if [ -z "$LD_LIBRARY_PATH" ]; then
		echo "false" && return
	fi

	if [ -z "$CGO_LDFLAGS" ]; then
		echo "false" && return
	fi

	if [ -z "$CGO_CFLAGS" ]; then
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

	LD_LIBRARY_PATH="${SL_PREFIX}/lib:${SL_SPDK_PREFIX}/lib:${LD_LIBRARY_PATH}"
	export LD_LIBRARY_PATH
	export CGO_LDFLAGS="-L${SL_SPDK_PREFIX}/lib -L${SL_PREFIX}/lib"
	export CGO_CFLAGS="-I${SL_SPDK_PREFIX}/include"
}

check=$(check_environment)

if [ "$check" == "false" ]; then
	setup_environment
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

oldGP=$GOPATH
export GOPATH=$DIR/../../build/src/control
echo "GOPATH $GOPATH"

repopath=github.com/daos-stack/daos

# Lint source then run Go tests for each package
for d in client security server dmg drpc lib/netdetect; do
    echo "testing $d"
    pushd "$GOPATH/src/$repopath/src/control/$d"
    # todo: provide a sensible way of linting and returning review comments
    # gofmt -l -s -w . && go tool vet -all . && golint && goimports -w .
    go test -v
    popd
done

export GOPATH=$oldGP
