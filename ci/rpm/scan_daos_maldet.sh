#!/bin/bash

set -uex

source /etc/os-release
: "${ID_LIKE:=unknown}"
: "${ID:=unknown}"
version="${VERSION_ID%%.*}"
if [[ $ID_LIKE == *rhel* ]]; then
  distro=el
fi
if [[ $ID == *leap* ]]; then
  distro=leap
fi

if command -v dnf; then
  sudo dnf -y install \
    daos{,-{client,server,tests,debuginfo,devel}}-"${DAOS_PKG_VERSION}"
elif command -v apt-get; then
  distro=ubuntu
  echo "Ubuntu not implemented yet."
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
malxml="maldetect_$distro$version.xml"
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
