#!/bin/bash
set -uex

# OBSOLETE ... this should be deleted

# Run from: docker run -it --name opensuse-rpm-builder -v ~/workspace/devops/packaging:/root/rpmbuild opensuse/leap:15.4 /bin/bash
# sudo docker rm opensuse-rpm-builder

# Set up the temporary local repository configuration with GPG check disabled
setup_local_repo() {
  local rpms_dst=$1

  echo "[local-rpms]
name=Local RPM Repository
baseurl=file://$rpms_dst
enabled=1
gpgcheck=0" > /etc/zypp/repos.d/local-rpms.repo
}

# Clean up the temporary repository configuration
cleanup_local_repo() {
  rm -f /etc/zypp/repos.d/local-rpms.repo
}

prepare_env() {

  if [ -f /tmp/setup_done ]; then
    return
  fi

  suse_version=${1:-"15.4"} # default to '15.4'

  if ! [[ "$suse_version" == "15.4" || "$suse_version" == "15.5" || "$suse_version" == "15.6" ]]; then
    echo "Unsupported SUSE Version: $suse_version"
    exit 1
  fi

  # Update and install required packages using zypper
  zypper refresh

  # Install needed packages
  # git so can clone the repo
  # rpm-build, rpmdevtools, createrepo for building the rpms and setting up the rpm repo
  # make is used for managing the rpmbuild process. This should probably be a dependency of the rpm.spec files, but not
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
  if ! zypper lr | grep -q "Main Repository"; then
      zypper ar -f http://download.opensuse.org/distribution/leap/$suse_version/repo/oss/ "Main Repository"
  else
      echo "Main Repository already exists, skipping addition."
  fi
  if ! zypper lr | grep -q "Main Update Repository"; then
      zypper ar -f http://download.opensuse.org/update/leap/$suse_version/oss/ "Main Update Repository"
  else
      echo "Main Update Repository already exists, skipping addition."
  fi

  # Ignore dpdk because want to use RPMs built from daos-stack/dpdk
  zypper update -y --skip-interactive --no-recommends
  touch /tmp/setup_done
}

install_dependencies() {
  repo_name=$1

   dependencies=$(rpmspec -q --buildrequires "$repo_name".spec | grep -v "^rpmlib" | grep -v "^/" | tr '\n' ' ')
      if [ -n "$dependencies" ]; then
          # Install dependencies using zypper
          echo "Installing build dependencies: $dependencies"
          zypper install --allow-vendor-change -y $dependencies
      else
          echo "No build dependencies found in the spec file."
      fi
}

# Function to clone, build, and install RPMs with packaging repos inside daos-stack
build_and_install_rpm() {
  local repo_name=$1
  local rpm_build_options=${2:-""}
  echo "Building $repo_name"

  # Check if already successful - helps if running script many times while troubleshooting
  if [ ! -f "$repo_name"/success ]; then
    if [ -d "$repo_name" ]; then rm -rf "$repo_name"; fi
    git clone https://github.com/daos-stack/"$repo_name".git
    cd "$repo_name"
      install_dependencies "$repo_name"
      make rpms RPM_BUILD_OPTIONS="$rpm_build_options"
      # zypper --no-gpg-checks install -y _topdir/RPMS/*/*.rpm
      cp -r _topdir/RPMS/* "$rpms_dst"/
      createrepo "$rpms_dst"
      zypper refresh
      touch success
    cd ..
  else
    echo "'$repo_name' dependency already built. Skipping step."
  fi
}

get_suse_version() {
  VERSION_ID=$(grep "^VERSION_ID" /etc/os-release | cut -d'=' -f2 | tr -d '"' | cut -d'.' -f1,2)
  echo $VERSION_ID
}

# --- #
# RUN #
# --- #
rpms_dst=${1:-"$HOME/rpms/"}
build_dst="/tmp/rpmbuild/" # temp location to do the building

echo "Building RPMs for openSUSE Leap $(get_suse_version)."
echo "RPMS will be located in $rpms_dst (built in $build_dst)"

# Make sure rpm and build folders are created
mkdir -p $rpms_dst
mkdir -p $build_dst

setup_local_repo "$rpms_dst"
createrepo "$rpms_dst"

# ------------------------------
# Build Dependency and DAOS RPMS
# ------------------------------
prepare_env "$(get_suse_version)"

cd $build_dst
# Call the function with the repository name as an argument
build_and_install_rpm "isa-l_crypto"
build_and_install_rpm "isa-l"
build_and_install_rpm "argobots"
build_and_install_rpm "libfabric"
build_and_install_rpm "mercury"
build_and_install_rpm "dpdk" # dependency of spdk.
build_and_install_rpm "spdk"
build_and_install_rpm "pmdk" "--nocheck"


echo "BUILDING DAOS"
# DAOS and Raft (Submodule)
if [ -d daos ]; then rm -rf daos; fi
git clone --recursive https://github.com/daos-stack/daos.git

# RAFT
cd daos/src/rdb/raft
  install_dependencies raft
  make -f Makefile-rpm.mk
#  zypper install --allow-vendor-change -y _topdir/RPMS/*/*.rpm
  cp -r _topdir/RPMS/* "$rpms_dst"/
  createrepo "$rpms_dst"
  zypper refresh
cd -

# DAOS
cd daos
  install_dependencies "utils/rpms/daos"
  make -C utils/rpms rpms
  cp -r utils/rpms/_topdir/RPMS/* "$rpms_dst"/
  createrepo "$rpms_dst"
  zypper refresh
cd -

# --------
# Clean up
# --------
cleanup_local_repo
rm -rf $build_dst