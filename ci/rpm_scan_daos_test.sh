#!/bin/bash

set -ex

# DAOS_PKG_VERSION environment variable needs to be set for this script

lmd_tarball='maldetect-current.tar.gz'
# todo: Move to tools repository
curl "http://rfxn.com/downloads/${lmd_tarball}" \
  -z ${lmd_tarball}  --silent --show-error --fail -O

nodelist=("${NODELIST//,/ }")
scp -i ci_key "${lmd_tarball}" "jenkins@${nodelist[0]}:/var/tmp"

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" "NODE=${nodelist[0]}  \
  DAOS_PKG_VERSION=$DAOS_PKG_VERSION                      \
  $(cat "$mydir/rpm_scan_daos_test_node.sh")"

rm -f "${WORKSPACE}/maldetect.xml"
scp -i ci_key jenkins@"${nodelist[0]}":/var/tmp/maldetect.xml \
  "${WORKSPACE}/maldetect.xml"
