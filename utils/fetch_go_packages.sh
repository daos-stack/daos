#!/bin/bash

function print_usage {
	echo "fetch_go_packages.sh [-i <path/to/daos>]" 1>&2
	echo "	-i: Path to top level DAOS source tree" 1>&2
	echo "	If no path is specified, current directory is used." 1>&2
}

DAOS_ROOT="."

while getopts ":i:" opt; do
	case "${opt}" in
		i)
			DAOS_ROOT=${OPTARG}
			;;
		*)
			print_usage
			exit 1
			;;
        esac
done

GOPATH=$(realpath -ms "$DAOS_ROOT")

cd "$DAOS_ROOT/src/control" && GOPATH="$GOPATH" dep ensure -v
