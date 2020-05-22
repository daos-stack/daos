#!/bin/bash

set -uex

# DAOS_PKG_VERSION environment variable needs to be set for this script

lmd_tarball='maldetect-current.tar.gz'

lmd_url="${JENKINS_URL}job/daos-stack/job/tools/job/master/"
lmd_url+="lastSuccessfulBuild/artifact/${lmd_tarball}"
curl "${lmd_url}" -z "./${lmd_tarball}"  --silent --show-error --fail -O

nodelist=(${NODELIST//,/ })

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

scp -i ci_key "${lmd_tarball}" "$mydir/rpm_scan_daos_test_node.sh" \
              "jenkins@${nodelist[0]}:/var/tmp"

ssh "$SSH_KEY_ARGS" jenkins@"${nodelist[0]}" \
 "NODE=${nodelist[0]}                \
  DAOS_PKG_VERSION=$DAOS_PKG_VERSION \
  /var/tmp/rpm_scan_daos_test_node.sh"

rm -f "${WORKSPACE}/maldetect.xml"
scp -i ci_key jenkins@"${nodelist[0]}":/var/tmp/maldetect.xml \
  "${WORKSPACE}/maldetect.xml"
