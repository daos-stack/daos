#!/bin/bash

set -uex

sudo yum -y install \
  daos{,-{client,server,tests,debuginfo,devel}}-"${DAOS_PKG_VERSION}"

lmd_src="maldet-current"
lmd_tarball="maldetect-current.tar.gz"
rm -rf "/var/tmp/${lmd_src}"
mkdir -p "/var/tmp/${lmd_src}"
tar -C "/var/tmp/${lmd_src}" --strip-components=1 -xf "/var/tmp/${lmd_tarball}"
pushd "/var/tmp/${lmd_src}"
  sudo ./install.sh
popd
sudo /usr/local/sbin/maldet --update-sigs

fc_conf="/etc/freshclam.conf"
if ! sudo grep -q 'ScriptedUpdates no' "$fc_conf"; then
  sudo -E bash -c "echo \"ScriptedUpdates no\" >> \"$fc_conf\""
fi
: "${JOB_URL:=${JENKINS_URL}job/clamav_daily_update/}"
if ! sudo grep -q "$JENKINS_URL" "$fc_conf"; then
  clam_url="${JOB_URL}lastSuccessfulBuild/artifact/download/clam"
  sudo -E bash -c "echo \"PrivateMirror ${clam_url}\" >> \"$fc_conf\""
fi
sudo freshclam
rm -f /var/tmp/clamscan.out
rm "/var/tmp/${lmd_tarball}"
rm -rf "/var/tmp/${lmd_src}"
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
rm -f /var/tmp/maldetect.xml
if grep 'Infected files: 0$' /var/tmp/clamscan.out; then
  cat << EOF_GOOD > /var/tmp/maldetect.xml
<testsuite skip="0" failures="0" errors="0" tests="1" name="Malware_Scan">
  <testcase name="Malware_scan" classname="ClamAV"/>
</testsuite>
EOF_GOOD
else
  cat << EOF_BAD > /var/tmp/maldetect.xml
<testsuite skip="0" failures="1" errors="0" tests="1" name="Malware_Scan">
  <testcase name="Malware_scan" classname="ClamAV">
    <failure message="Malware Detected" type="error">
      <![CDATA[ "$(cat /var/tmp/clamscan.out)" ]]>
    </failure>
  </testcase>
</testsuite>
EOF_BAD
fi
