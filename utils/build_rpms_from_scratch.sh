#!/bin/bash
set -uex

###############################################################################
# DAOS RPM Build Script
#
# Description:
# This script sets up an environment to build and install RPM packages for DAOS
# and its dependencies on a Rocky Linux system. It automates the installation
# of required tools, package dependencies, and specific repositories, ensuring
# that each RPM is built in a temporary location and copied to a destination
# directory.
#
# Usage:
#   Run the script in a Rocky Linux system
#
# Arguments:
#   1. Destination for built RPMs (default: "$HOME/rpm/")
#
###############################################################################

dnf_builddep() {
  local rpms_dst=$1
  local spec_file="$2"
  dnf --nogpgcheck --repofrompath rpms,"$rpms_dst" --refresh builddep -y "$spec_file"
}

setup_rpm_build_env() {
  el_version=${1:-8} # default to '8'

  # Update and install required packages
  dnf install -y \
      git \
      rpm-build \
      rpmdevtools \
      dnf-plugins-core \
      epel-release

  # Install build tools and dependencies
  dnf groupinstall -y "Development Tools"
  dnf install -y mock epel-release

  # enable Power Tools or CodeReady Builder depending on el version
  if [[ "$el_version" == "8" ]]; then
    dnf config-manager --set-enabled powertools
  elif [[ "$el_version" == "9" ]]; then
    dnf config-manager --set-enabled crb # CodeReady Builder
  else
    echo "Unsupported EL Version: $el_version"
    exit 1
  fi

  # ignore dpdk because want to use rpms build from daos-stack/dpdk
  dnf update -y --exclude=dpdk --exclude=dpdk-devel
}

# Function to clone a daos-stack repo, build rpms, and setup the rpm repository
build_and_install_rpm() {
  local repo_name=$1
  local rpm_build_options=${2:-""}

  # Check if already successful - helps if running script many times while troubleshooting
  if [ ! -f "$repo_name"/success ]; then
    if [ -d "$repo_name" ]; then rm -rf "$repo_name"; fi
    echo "Building $repo_name ..."
    git clone https://github.com/daos-stack/"$repo_name".git
    cd "$repo_name"
      dnf_builddep "$rpms_dst" "$repo_name".spec
      make rpms RPM_BUILD_OPTIONS="$rpm_build_options"
      cp -r _topdir/RPMS/* "$rpms_dst"/
      createrepo "$rpms_dst"
      touch success
    cd ..
  else
    echo "'$repo_name' dependency already built. Skipping step."
  fi
}

get_el_version() {
  VERSION_ID=$(grep "^VERSION_ID" /etc/os-release | cut -d'=' -f2 | tr -d '"' | cut -d'.' -f1)
  echo "$VERSION_ID"
}

# ---- #
# MAIN #
# ---- #
rpms_dst=${1:-"$HOME/rpms/"}
build_dst="/tmp/rpmbuild/" # temp location to do the building

echo "Building RPMs for EL $(get_el_version)."
echo "RPMS will be located in $rpms_dst (built in $build_dst)"

# make sure rpm and build folders are created
mkdir -p "$rpms_dst" "$build_dst"

# ------------------------------
# Build Dependency and DAOS RPMS
# ------------------------------
setup_rpm_build_env "$(get_el_version)"

cd $build_dst

# Initialize the rpm repo
createrepo "$rpms_dst"

# Call the function with the repository name as an argument
build_and_install_rpm "isa-l_crypto"
build_and_install_rpm "dpdk" # dependency of spdk.
build_and_install_rpm "spdk"
build_and_install_rpm "argobots"
build_and_install_rpm "mercury"
build_and_install_rpm "pmdk" "--nocheck"

# DAOS and Raft (Submodule)
git clone --recursive https://github.com/daos-stack/daos.git

# RAFT
if [ ! -f daos/src/rdb/raft/success ]; then
  cd daos/src/rdb/raft
    dnf_builddep "$rpms_dst" raft.spec
    make -f Makefile-rpm.mk
    cp -r _topdir/RPMS/* "$rpms_dst"/
    createrepo "$rpms_dst"
    touch success
  cd -
fi

# DAOS
if [ ! -f daos/success ]; then
  cd daos
  dnf_builddep "$rpms_dst" ./utils/rpms/daos.spec
  make -C utils/rpms rpms
  cp -r utils/rpms/_topdir/RPMS/* "$rpms_dst"/
  createrepo "$rpms_dst"
  touch success
  cd -
fi

# --------
# Clean up
# --------
rm -rf $build_dst
