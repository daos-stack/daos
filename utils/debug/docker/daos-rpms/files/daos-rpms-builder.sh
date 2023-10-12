#!/bin/bash

# set -x
set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

BUILD_TYPE=debug
GIT_BRANCH=master
SCONS_ARGS=()
declare -A TOPDIRS
TOPDIRS[daos]=/home/daos/daos-build/utils/rpms/_topdir
TOPDIRS[raft]=/home/daos/daos-build/src/rdb/raft/_topdir
TOPDIRS[pmdk]=/home/daos/pmdk-build/_topdir
TOPDIRS[spdk]=/home/daos/spdk-build/_topdir
TOPDIRS[mercury]=/home/daos/mercury-build/_topdir
TOPDIRS[argobots]=/home/daos/argobots-build/_topdir
TOPDIRS[isal_crypto]=/home/daos/isal_crypto-build/_topdir
TOPDIRS[isal]=/home/daos/isal-build/_topdir
TOPDIRS[dpdk]=/home/daos/dpdk-build/_topdir
TOPDIRS[libfabric]=/home/daos/libfabric-build/_topdir

function usage ()
{
	cat <<- EOF
	usage: daos-rpms-builder [OPTIONS] [-- SCONS_ARGS]

	Build DAOS rpms and its dependencies

	Options:
		-o, --outdir <dir>			Output directory
		-t, --type <type>			Type of build (e.g. debug, release...)
		-b, --branch <branch name>		GIT branch of the DAOS repo to build
		-h, --help				Show this help message and exit
	EOF
}

OPTIONS=$(getopt -o "o:t:b:h" --long "output:type:,branch:,help" -- "$@") || exit 1
eval set -- "$OPTIONS"
while true
do
	case "$1" in
		-o|--outdir) OUTDIR="$2" ; shift 2 ;;
		-t|--type) BUILD_TYPE="$2" ; shift 2 ;;
		-b|--branch) GIT_BRANCH="$2" ; shift 2 ;;
		-h|--help) usage ; exit 0;;
		--) shift 1 ; SCONS_ARGS=("$@") ; break ;;
		*) fatal "unrecognized command line option: $1" ;;
	esac
done

if [[ ! ${OUTDIR:+x} ]] ; then
	echo '[ERROR] Missing output directory'
	exit 1
fi
if [[ ! -d "$OUTDIR" ]] ; then
	echo "[ERROR] Invalid outptut directory $OUTDIR"
	exit 2
fi

SCONS_ARGS+=("TARGET_TYPE=default" "BUILD_TYPE=$BUILD_TYPE")
EXTERNAL_RPM_BUILD_OPTIONS=" --define \"scons_args ${SCONS_ARGS[*]}\""

echo "[INFO] Switching to DAOS GIT branch $GIT_BRANCH"
cd /home/daos/daos-build
if [[ -z $(git branch --list "$GIT_BRANCH") ]] ; then
	git switch --recurse-submodules --create="$GIT_BRANCH" "origin/$GIT_BRANCH"
else
	git switch --recurse-submodules "$GIT_BRANCH"
fi

echo "[INFO] Installing RAFT build dependencies"
sudo dnf build-dep --spec /home/daos/daos-build/src/rdb/raft/raft.spec

echo "[INFO} Building RAFT rpms"
cd /home/daos/daos-build/src/rdb/raft
make -f Makefile-rpm.mk

echo "[INFO] Installing RAFT rpms"
sudo dnf install /home/daos/daos-build/src/rdb/raft/_topdir/RPMS/x86_64/*.rpm

echo "[INFO] Installing DAOS build dependencies"
sudo dnf build-dep --spec /home/daos/daos-build/utils/rpms/daos.spec

echo "[INFO] Build DAOS rpms"
cd /home/daos/daos-build/utils/rpms
make -j $(nproc) rpms SCONS_ARGS="$SCONS_ARGS" EXTERNAL_RPM_BUILD_OPTIONS="$EXTERNAL_RPM_BUILD_OPTIONS"

for key in ${!TOPDIRS[@]} ; do
	dir_path="$OUTDIR/$key"
	sudo mkdir -p "$dir_path"
	echo "[INFO] Copy rpms of $key into $dir_path"
	topdir="${TOPDIRS[$key]}"
	for file_path in $(find "$topdir" -type f -name "*.rpm" -print) ; do
		sudo install -pv "$file_path" "$dir_path"
	done
done
