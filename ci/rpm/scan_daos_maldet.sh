#!/bin/bash

set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# at some point we want to use: shellcheck source=ci/rpm/distro_info.sh
# shellcheck disable=SC1091
source "$mydir/distro_info.sh"
# shellcheck disable=SC1091
source "$mydir/post_provision_config_common_functions.sh"

if command -v dnf; then
  sudo retry_cmd 360 dnf -y install \
    daos{,-{client,server,tests,debuginfo,devel}}-"${DAOS_PKG_VERSION}"
elif command -v apt-get; then
  echo "Ubuntu not implemented yet."
  exit 1
else
  echo "Unknown distribution."
  exit 1
fi

sudo clamscan -d /usr/local/maldetect/sigs/rfxn.ndb    \
              -d /usr/local/maldetect/sigs/rfxn.hdb -r \
              --exclude-dir=/.snapshots                \
              --exclude-dir=/usr/local/maldetect       \
              --exclude-dir=/usr/share/clamav          \
              --exclude-dir=/var/lib/clamav            \
              --exclude-dir=/sys                       \
              --exclude-dir=/proc                      \
              --exclude-dir=/dev                       \
              --exclude-dir=/scratch                   \
              --infected / | tee /var/tmp/clamscan.out
malxml="maldetect_$PUBLIC_DISTRO$MAJOR_VERSION.xml"
rm -f "$malxml"
if grep 'Infected files: 0$' /var/tmp/clamscan.out; then
  cat << EOF_GOOD > "$malxml"
<testsuite skip="0" failures="0" errors="0" tests="1" name="Malware_Scan">
  <testcase name="Malware_scan" classname="ClamAV"/>
</testsuite>
EOF_GOOD
else
  cat << EOF_BAD > "$malxml"
<testsuite skip="0" failures="1" errors="0" tests="1" name="Malware_Scan">
  <testcase name="Malware_scan" classname="ClamAV">
    <failure message="Malware Detected" type="error">
      <![CDATA[ "$(cat /var/tmp/clamscan.out)" ]]>
    </failure>
  </testcase>
</testsuite>
EOF_BAD
fi
