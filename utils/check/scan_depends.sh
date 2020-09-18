#!/bin/bash

# This script installs the Red Hat CVS API tool and then runs
# it against a list of packages in the file ${PACKAGES} and produces
# a file ${CVE_LIST} with the CVE information found.
#
# Default for ${PACKAGES} is "daos_depends_packages"
# Default for ${CVE_LIST} is "daos_depends_cve_list"

set -uex

: "${PACKAGES:="daos_depends_packages"}"
: "${CVE_LIST:="daos_depends_cve_list"}"

sudo yum -y install git
if [ ! -e rhsecapi ]; then
  git clone https://github.com/RedHatOfficial/rhsecapi.git
else
  pushd rhsecapi
    git pull
  popd
fi

# Need to add this file.
if [ ! -e /etc/rhsecapi-no-argcomplete ]; then
  sudo touch /etc/rhsecapi-no-argcomplete
fi

if [ -e "${CVE_LIST}" ]; then
  rm "${CVE_LIST}"
fi

while read dp; do
  echo "Checking package ${dp%-*-*}" >> "${CVE_LIST}"
  rhsecapi/rhsecapi.py --q-package "${dp%-*-*}" \
     --extract-cves --product 'linux 7' \
     -f bugzilla,fix_states,severity,cvss &>> "${CVE_LIST}"
done < "${PACKAGES}"

cat "${CVE_LIST}"

