#!/bin/bash

set -uex

# DAOS_PKG_VERSION environment variable needs to be set for this script

lmd_tarball='maldetect-current.tar.gz'

lmd_url="${JENKINS_URL}job/daos-stack/job/tools/job/master/"
lmd_url+="lastSuccessfulBuild/artifact/${lmd_tarball}"
curl "${lmd_url}" -z "./${lmd_tarball}"  --silent --show-error --fail -O

nodelist=(${NODELIST//,/ })

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

scp -i ci_key "${lmd_tarball}" "jenkins@${nodelist[0]}:/var/tmp"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"${nodelist[0]}" \
  "NODE=${nodelist[0]}                       \
   DAOS_PKG_VERSION=$DAOS_PKG_VERSION        \
   JENKINS_URL=$JENKINS_URL                  \
   $(cat "$mydir/scan_daos_node.sh")"

rm -f "${WORKSPACE}/maldetect.xml"
scp -i ci_key jenkins@"${nodelist[0]}":/var/tmp/maldetect.xml \
  "${WORKSPACE}/maldetect.xml"
