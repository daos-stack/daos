#!/bin/bash
# this script is meant to be used from a GitHub Action. The outcome should be a zip file of the
# built binaries, similar to how the jenkins step (sconsBuild.grovy) does it
# expect to be in the daos dir

set -uex
echo "hostname: $(hostname)"
echo "pwd: $(pwd)"

tar_name=${1:-"opt-daos.tar"} # opt-daos.tar is what sconsBuild.grovy names it and what the unit tests look for

# install build dependencies
sudo dnf --assumeyes install dnf-plugins-core
sudo dnf config-manager --save --setopt=assumeyes=True
sudo ./utils/scripts/install-el8.sh
sudo python3 -m pip install -r requirements-build.txt

# build
scons-3 --build-deps=yes --config=force -j12 install

# need to simulate: 'tar -cf opt-daos.tar /opt/daos/' (this is how the sconsBuild.grovy does it in Jenkins)
# will be unpacked with: tar --strip-components=2 --directory /opt/daos -xf opt-daos.tar
# so need have two levels of directory (components) that will be striped
parent=$(basename "$PWD")
tar -cf "$tar_name" ../"$parent"/install

set +e

