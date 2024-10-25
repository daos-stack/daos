#!/bin/bash
# this script is meant to be used from a GitHub Action. The outcome should be a zip file of the
# built binaries, similar to how the jenkins step (sconsBuild.grovy) does it

set -e
echo "hostname: $(hostname)"
echo "pwd: $(pwd)"

src_dir=${1:-"build"}
tar_name=${2:-"opt-daos.tar"} # opt-daos.tar is what sconsBuild.grovy names it and what the unit tests look for

cd "$src_dir"
# install build dependencies
sudo dnf --assumeyes install dnf-plugins-core
sudo dnf config-manager --save --setopt=assumeyes=True
sudo ./utils/scripts/install-el8.sh
sudo python3 -m pip install -r requirements-build.txt

# build
scons-3 --build-deps=yes --config=force -j12 install

# need to simulate: 'tar -cf opt-daos.tar /opt/daos/' (this is how the sconsBuild.grovy does it in Jenkins)
# will be unpacked with: tar --strip-components=2 --directory /opt/daos -xf opt-daos.tar
tar -cf "$tar_name" -C ./build/ install
mv "$tar_name" ./build/

set +e

