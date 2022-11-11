#!/bin/bash

# Installs maldet, clamAV and uses the current Jenkins job
# as an artifact server.
#
# Makes an effort to update the current maldet database.
# it is not fatal if the maldet database can not be updated.

set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=utils/scripts/helpers/distro_info.sh
source "$mydir/distro_info.sh"

# We need sudo for running the scan and git for backward
# compatibility.
# maldet uses which internally
if command -v dnf; then
  dnf install clamav clamav-devel findutils git gzip hostname sudo which
  # Some Dockerfiles missing the md5sum command by not having
  # coreutils installed.   Rocky 8 pre-installs coreutils-single which
  # blocks coreutils from being installed.
  if ! command -v md5sum; then
      dnf install coreutils
  fi
elif command -v apt-get; then
  apt-get --assume-yes install clamav libclamav-dev \
                       coreutils findutils git gzip hostname sudo which
  service clamav-freshclam stop || true
  systemctl disable clamav-freshclam || true
fi
mkdir -p /etc/clamd.d
printf "LogSyslog yes\n" >> /etc/clamd.d/scan.conf

lmd_tarball='maldetect-current.tar.gz'
: "${JENKINS_URL:=https://build.hpdd.intel.com/}"

lmd_url="${JENKINS_URL}job/daos-stack/job/tools/job/master/"
lmd_url+="lastSuccessfulBuild/artifact/${lmd_tarball}"
curl "${lmd_url}" --silent --show-error --fail -o "/var/tmp/${lmd_tarball}"

lmd_src='lmd_src'
mkdir -p "/var/tmp/${lmd_src}"
tar -C "/var/tmp/${lmd_src}" --strip-components=1 -xf "/var/tmp/${lmd_tarball}"
pushd "/var/tmp/${lmd_src}"
  ./install.sh
popd

/usr/local/sbin/maldet --update-sigs || true

printf "ScriptedUpdates no\n" >> /etc/freshclam.conf
: "${JOB_URL:=https://build.hpdd.intel.com/job/clamav_daily_update/}"
printf "PrivateMirror %s" \
       "${JOB_URL}lastSuccessfulBuild/artifact/download/clam" \
       >> /etc/freshclam.conf

freshclam

rm "/var/tmp/${lmd_tarball}"
rm -rf "/var/tmp/${lmd_src}"
