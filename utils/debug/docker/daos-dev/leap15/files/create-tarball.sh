#!/bin/bash

# set -x
set -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"
ARCHIVE_NAME=${1:?"Define an archive name"}
BUILD_TYPE=${2:-"debug"}
if ! [[ "$BUILD_TYPE" =~ ^(debug|release)$ ]] ; then
	echo "[ERROR] Invalid build type \"$BUILD_TYPE\": accepted values \"debug\" \"release\""
	exit 1
fi
TARGET=${3:-"leap15"}
DISTRO_VERSION=${4:-"15.5"}

SCONS_ARGS="TARGET_TYPE=default BUILD_TYPE=$BUILD_TYPE ADDRESS_SANITIZER=1 STACK_MMAP=1"
EXTERNAL_RPM_BUILD_OPTIONS=" --define \"scons_args $SCONS_ARGS\""

echo
echo "[INFO] Creating workspace..."
ARCHIVE_DIR=$CWD/tmp/$ARCHIVE_NAME
rm -frv "$ARCHIVE_DIR" "$CWD/_topdir"
mkdir -pv "$ARCHIVE_DIR"

echo
echo "[INFO] Building rpms..."
cd "$CWD"
env TARGET=$TARGET make -j $(nproc) rpms SCONS_ARGS="$SCONS_ARGS" EXTERNAL_RPM_BUILD_OPTIONS="$EXTERNAL_RPM_BUILD_OPTIONS" DISTRO_VERSION=$DISTRO_VERSION

echo
echo "[INFO] Populating workspace..."
RPMS=$(find "$CWD/_topdir" -type f -name "*.rpm")
cp -av $RPMS "$ARCHIVE_DIR"

echo
echo "[INFO] Creating archive..."
tar -c -J -v --directory="$CWD/tmp" -f "$ARCHIVE_NAME.txz" "$ARCHIVE_NAME"
