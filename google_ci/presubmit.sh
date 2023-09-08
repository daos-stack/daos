#!/usr/bin/env bash

set -eExo pipefail

function main() {
  echo "Installing packages..."
  dnf install -y sudo git virtualenv epel-release dnf-plugins-core
  dnf config-manager --enable powertools
  dnf config-manager --save --setopt=assumeyes=True

  echo "Running install script..."
  utils/scripts/install-el8.sh

  echo "Installing python dependencies..."
  virtualenv venv
  source venv/bin/activate
  pip install --upgrade pip
  pip install -r requirements.txt

  echo "Building daos..."
  scons BUILD_TYPE=dev TARGET_TYPE=release --jobs="$(nproc --all)" --build-deps=yes install PREFIX=/opt/daos
}

main "$@"
