#!/bin/bash

set -uex

# This file does an install of the RPMs found in "${REPO_DIR}" and then
# finds all the packages that the binaries installed reference.

# The file ${PACKAGES} will contain a list of packages that the RPMs depend
# on.

# The file ${SOFILES} will contain a list of shareable images depended on by
# the RPMs.  The default name for ${SOFILES} is "daos_depends_libries".

# This will likely need to be edited for other build systems.

: "${WORKSPACE:="${PWD}"}"

: "${RPM_DIR:="${WORKSPACE}/rpm_dir"}"

: "${SOFILES:="daos_depends_libraries"}"
: "${PACKAGES:="daos_depends_packages"}"
: "${CHECKED_FILES:="daos_checked_files"}"

repo_url="https://repo.dc.hpdd.intel.com/repository/"
repo_url+="daos-stack-el-7-x86_64-stable-local"

: "${DAOS_STACK_EL_7_LOCAL_REPO:="${repo_url}"}"

repo_dir="/etc/yum.repos.d/"
repo_base1="${DAOS_STACK_EL_7_LOCAL_REPO#*/*/*}"
repo_file="${repo_dir}${repo_base1//\//_}.repo"

if [ ! -e "${repo_file}" ]; then
  sudo yum -y install yum-utils
  sudo yum-config-manager --add-repo "${DAOS_STACK_EL_7_LOCAL_REPO}"
fi
if ! grep "gpgcheck = false" "${repo_file}"; then
  echo "gpgcheck = false" | sudo tee -a "${repo_file}"
fi

sudo yum -y remove cart cart-debuginfo cart-devel cart-tests \
                   daos daos-client daos-debuginfo daos-devel daos-server \
                   daos-tests CUnit dpdk fio fuse fuse-lib hwloc \
                   libabt0 libcmocka libfabric libfabric-devel libisa-l \
                   libisa-l_crypto libopa1 libpmem libpmemblk mercury ndctl\
                   ompi openmpi3 \
                   pexpect protobuf-c pmix \
                   python-configshell python-urwid \
                   raft raft-debuginfo raft-devel \
                   spdk spdk-tools

sudo yum -y install "${RPM_DIR}"/*.rpm

set +x

# Step 1. Get a list of all the files installed by the RPMs that
#         we just installed.
f_list=""

for f in "${RPM_DIR}"/*.rpm; do
  rpm_files="$(rpm -qlp "${f}")"
  f_list+=" ${rpm_files}"
done

# Step 2. Identify which of the files are ELF executables and
#         lookup what images that they depend on.
rm -f raw_image_depends
rm -f "${CHECKED_FILES}"

for f in ${f_list}; do
  if [[ -x "${f}" ]]; then
    if file "${f}" | grep " ELF "; then
      ldd "${f}" >> raw_lib_depends
      hardening-check --quiet --nopie --nobindnow --nofortify "${f}" | \
        tee -a "${CHECKED_FILES}" || true
    fi
  fi
done

# Step 3. Strip out just the filenames, sort removing duplicates
awk '{print $1}' raw_lib_depends | sort -u > "${SOFILES}"

# Step 4. Find the package that provided the dependent file.
rm -rf raw_pkg_depends

while read dp; do
  if [ "${dp}" != linux-vdso.so.1 ]; then
    if [[ ${dp} = /* ]]; then
      #echo "Testing ${dp}"
      rpm -qf "/usr/${dp}" >> raw_pkg_depends
    else
      #echo "testing ${dp}"
      dp_found="$(sudo find /usr -name "${dp}")"
      for dpf in ${dp_found}; do
        rpm -qf "${dpf}" >> raw_pkg_depends
      done
    fi
  fi
done < "${SOFILES}"

sort -u raw_pkg_depends > "${PACKAGES}"

