#!/usr/bin/env bash

function build_daos() {
  if [ -d /opt/daos ]; then
    echo "DAOS appears to already be installed in /opt/daos"
  else
    set -e

    echo "Checking utils/build.config for changes"
    # This should fail if an update to a dependency githash is needed
    utils/docker/gcp/get_rpm_hash.sh libfabric

    echo "Installing packages..."
    dnf install -y sudo git virtualenv epel-release dnf-plugins-core procps-ng hostname
    dnf config-manager --enable powertools
    dnf config-manager --save --setopt=assumeyes=True

    echo "Running install script..."
    utils/scripts/install-el8.sh

    echo "Installing go dependencies..."
    GOBIN=/usr/bin go install gotest.tools/gotestsum@latest

    echo "Installing python dependencies..."
    virtualenv venv
    source venv/bin/activate
    pip install --require-hashes -r utils/docker/gcp/base_requirements.txt
    pip install --require-hashes -r utils/docker/gcp/requirements.txt

    echo "Building daos..."
    scons BUILD_TYPE=dev TARGET_TYPE=release --jobs="$(nproc --all)" --build-deps=yes install PREFIX=/opt/daos

    pip install /opt/daos/lib/daos/python/
    set +e
  fi
}

function check_test_user() {
   if [ -z "${TEST_USER}" ]; then
    echo "TEST_USER is not set"
    exit 1
  fi
}

function create_test_user() {
  test_user=${1:-"testuser"}
  if ! getent passwd "${test_user}" > /dev/null; then
    # Create test user
    echo "Creating test user (${test_user})..."
    useradd -m -s /bin/bash "${test_user}"
    cat <<EOF > /etc/sudoers.d/"${test_user}"
${test_user} ALL=(ALL) NOPASSWD: ALL
EOF
  else
    echo "Test user (${test_user}) already exists."
  fi

  export TEST_USER="${test_user}"
}
