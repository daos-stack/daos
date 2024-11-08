#!/bin/bash
set -uex

###############################################################################
# DAOS RPM Build Script
#
# Description:
# This script sets up an environment to build and install RPM packages for DAOS
# and its dependencies. The script serves as an example of what dependencies are
# needed for the DAOS RPMs, but it has been tested on rockylinux 8 and 9, and on
# opensuse/leap 15.4, 15.5, and 15.6.
#
# Arguments:
#   1. Destination for built RPMs (default: "$HOME/rpm/")
#
###############################################################################

# Get distro information so can be used throughout the script globally
. /etc/os-release
OS_NAME=$NAME
OS_VERSION=$VERSION_ID
OS_ID=$ID

# -----------------------------------------------------------------------------
# This first set of functions serve to abstract away some OS specifics. It is
# assumed that by the time these functions are called, the OS and Version
# have been verified
# -----------------------------------------------------------------------------

# Set up the temporary local repository configuration with GPG check disabled
setup_local_repo() {
  local rpms_dst=$1

  if [[ "$OS_ID" == "opensuse-leap" ]]; then
    repo_file=/etc/zypp/repos.d/local-rpms.repo
  else
    repo_file=/etc/yum.repos.d/local-rpms.repo
  fi

  echo "[local-rpms]
name=Local RPM Repository
baseurl=file://$rpms_dst
enabled=1
gpgcheck=0" > "$repo_file"
}

# Clean up the temporary repository configuration
cleanup_local_repo() {
  if [[ "$OS_ID" == "opensuse-leap" ]]; then
    rm -f /etc/zypp/repos.d/local-rpms.repo
  else
    rm -f /etc/yum.repos.d/local-rpms.repo
  fi
}

install_dependencies() {
  local spec_file="$1"

  if [[ "$OS_ID" == "opensuse-leap" ]]; then
   dependencies=$(rpmspec -q --buildrequires "$spec_file" | grep -v "^rpmlib" | grep -v "^/" | tr '\n' ' ')
    if [ -n "$dependencies" ]; then
        # Install dependencies using zypper
        zypper install --allow-vendor-change -y $dependencies
    fi
  else
    dnf builddep -y "$spec_file"
  fi
}

prepare_env_leap() {
  suse_version=$OS_VERSION # default to '15.4'

  # Update and install required packages using zypper
  zypper refresh

  # Install needed packages
  # git so can clone the repo
  # rpm-build, rpmdevtools, createrepo for building the rpms and setting up the rpm repo
  # make is used for managing the rpmbuild process.
  # python3-Sphinx is needed by dpdk. The file /usr/bin/sphinx-build is listed as a dependency in the spec file,
  #   however, zypper isn't able to resolve a filename to a package and the scripting to handle that case is pretty
  #   messy, especially because "zypper what-provides" actually returns three options for sphinx-build. So, forgoing
  #   automation here and simply hardcoding the dependency.
  zypper install -y \
      git \
      rpm-build \
      rpmdevtools \
      createrepo_c \
      make \
      python3-Sphinx


  # Install build tools and dependencies
  zypper install -y -t pattern devel_basis

  # Enable additional repositories if needed

  declare -A repos
  repos["Main Repository"]="distribution/leap/$suse_version/repo/oss"
  repos["Main Update Repository"]="update/leap/$suse_version/oss"

  for repo in "${!repos[@]}"; do
    if ! zypper lr | grep -q "$repo"; then
      zypper ar -f http://download.opensuse.org/"${repos[$repo]}" "$repo"
    else
      echo "$repo already exists, skipping addition."
    fi
  done

  zypper update -y --skip-interactive --no-recommends
}

prepare_env_el() {
  el_version=$OS_VERSION # default to '8'

    # Install needed packages
    # git so can clone the repo
    # rpm-build, rpmdevtools, createrepo for building the rpms and setting up the rpm repo
    # dnf-plugins-core for additional dnf capabilities
    # make is used for managing the rpmbuild process.
  dnf install -y \
      git \
      rpm-build \
      rpmdevtools \
      createrepo \
      dnf-plugins-core \
      epel-release \
      make # shouldn't be needed here, but dependency spec files don't include

  # enable Power Tools or CodeReady Builder depending on el version. Needed for yasm and maybe others
  if [[ "$el_version" == "8" ]]; then
    dnf config-manager --set-enabled powertools
  elif [[ "$el_version" == "9" ]]; then
    dnf config-manager --set-enabled crb # CodeReady Builder
  fi

  # ignore dpdk because want to use rpms build from daos-stack/dpdk
  dnf update -y --exclude=dpdk --exclude=dpdk-devel
}

l_createrepo() {
  createrepo "$1"

  if [[ "$OS_ID" == "opensuse-leap" ]]; then
    zypper refresh --repo local-rpms
  else
    dnf --disablerepo=\* --enablerepo=local-rpms makecache
  fi
}

prepare_env() {
  if [ -f /tmp/setup_done ]; then return; fi

  rpms_dst=$1
  build_dst=$2

  mkdir -p "$rpms_dst" "$build_dst"

  if [[ "$OS_ID" == "opensuse-leap" ]]; then
      prepare_env_leap
    else
      prepare_env_el
  fi

  setup_local_repo "$rpms_dst"
  l_createrepo "$rpms_dst"

  touch /tmp/setup_done
}

verify_os_version() {
  if [[ "$OS_ID" == "opensuse-leap" ]]; then
    if ! [[ "$OS_VERSION" == "15.4" || "$OS_VERSION" == "15.5" || "$OS_VERSION" == "15.6" ]]; then
      echo "Untested SUSE Version: $OS_VERSION"
      exit 1
    fi
  elif [[ "$OS_ID" == "rocky" ]]; then
    if ! [[ "$OS_VERSION" == "8.9" || "$OS_VERSION" == "9.3" ]]; then
      echo "Untested Rocky Version: $OS_VERSION"
      exit 1
    fi
  else
    echo "Untested OS: $OS_ID"
    exit 1
  fi
}

# -----------------------------------------------------------------------------
# The following functions should be common, and not have OS specific calls
# -----------------------------------------------------------------------------

# Function to clone a daos-stack repo, build rpms, and setup the rpm repository
build_and_copy_rpm() {
  local repo_name=$1

  # Check if already successful - helps if running script many times while troubleshooting
  if [ ! -f "$repo_name"/success ]; then
    if [ -d "$repo_name" ]; then rm -rf "$repo_name"; fi
    echo "Building $repo_name ..."
    git clone https://github.com/daos-stack/"$repo_name".git
    cd "$repo_name"
      install_dependencies "$repo_name".spec
      make rpms RPM_BUILD_OPTIONS="--nocheck" # --nocheck = don't run tests during rpm build process
      cp -r _topdir/RPMS/* "$rpms_dst"/
      l_createrepo "$rpms_dst"
      touch success
    cd ..
  else
    echo "'$repo_name' dependency already built. Skipping step."
  fi
}

main() {
  verify_os_version

  rpms_dst=${1:-"$HOME/rpms/"}
  build_dst="/tmp/rpmbuild/" # temp location to do the building

  echo "Building RPMs for $OS_NAME $OS_VERSION."
  echo "RPMS will be located in $rpms_dst (built in $build_dst)"

  prepare_env "$rpms_dst" "$build_dst"
  cd "$build_dst"

  for pkg in isa-l isa-l_crypto dpdk spdk argobots mercury pmdk; do
    build_and_copy_rpm "$pkg"
  done

  # DAOS and Raft (Submodule)
  git clone --recursive https://github.com/daos-stack/daos.git

  # RAFT
  if [ ! -f daos/src/rdb/raft/success ]; then
    cd daos/src/rdb/raft
      install_dependencies raft.spec
      make -f Makefile-rpm.mk
      cp -r _topdir/RPMS/* "$rpms_dst"/
      l_createrepo "$rpms_dst"
      touch success
    cd -
  fi

  # DAOS
  if [ ! -f daos/success ]; then
    cd daos
    install_dependencies ./utils/rpms/daos.spec
    make -C utils/rpms rpms
    cp -r utils/rpms/_topdir/RPMS/* "$rpms_dst"/
    l_createrepo "$rpms_dst"
    touch success
    cd -
  fi

  # --------
  # Clean up
  # --------
  cleanup_local_repo
  rm -rf $build_dst
}

main "$@"
