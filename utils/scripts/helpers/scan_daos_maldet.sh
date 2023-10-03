#!/bin/bash

set -uex

# Install the DAOS RPMs and then do a malware scan
# of the resulting system volume.

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=utils/scripts/helpers/distro_info.sh
source "$mydir/distro_info.sh"

if command -v dnf; then
    if [ -n "$DAOS_PKG_VERSION" ]; then
        DAOS_PKG_VERSION="-$DAOS_PKG_VERSION"
    else
        # don't need artifactory if no version was specified,
        # as that means we are using the packages in the build
        . /etc/os-release
        dnf -y config-manager \
            --disable daos-stack-daos-"${DISTRO_GENERIC}"-"${VERSION_ID%%.*}"-x86_64-stable-local-artifactory
    fi
    sudo dnf install daos{,-{client,server,tests,debuginfo,devel}}"$DAOS_PKG_VERSION"
elif command -v apt-get; then
    echo "Ubuntu not implemented yet."
    exit 1
else
    echo "Unknown distribution."
    exit 1
fi
fails=0
errs=0
mal_fnd=""
if ! sudo /usr/local/sbin/maldet --update-sigs; then
    ((fails+=1)) || true
    mal_fnd='<failure message="Maldet signature update failed" type="warning"/>'
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
clam_fnd=""
if ! grep 'Infected files: 0$' /var/tmp/clamscan.out; then
    ((errs+=1)) || true
    clam_fnd="<error message=\"Malware Detected\" type=\"error\">
  <![CDATA[ $(cat /var/tmp/clamscan.out) ]]>
</error>"
fi

cat << EOF > "$malxml"
<testsuite skip="0" failures="$fails" errors="$errs" tests="2" name="Malware_Scan">
  <testcase name="Maldet update" classname="Maldet">
    $mal_fnd
  </testcase>
  <testcase name="Malware_scan" classname="ClamAV">
    $clam_fnd
  </testcase>
</testsuite>
EOF
