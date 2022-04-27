#!/bin/bash
# Copyright 2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# Install mpifileutils
#
# This script assumes that the intel-oneapi-mpi and intel-oneapi-mpi-devel
# packages from https://yum.repos.intel.com/oneapi have already been installed
# on the system.
#

set -e
trap 'echo "An unexpected error occurred. Exiting."' ERR

INSTALL_ROOT_DIR="${INSTALL_ROOT_DIR:-/usr/local}"
DAOS_INSTALL_PATH="${DAOS_INSTALL_PATH:-/usr}"
TOOLS_DIR="${TOOLS_DIR:-${INSTALL_ROOT_DIR}/tools}"

MFU_ROOT_DIR="${INSTALL_ROOT_DIR}/mpifileutils"
MFU_DEPS_DIR="${MFU_ROOT_DIR}/deps"
MFU_SRC_DIR="${MFU_ROOT_DIR}/src"
MFU_BUILD_DIR="${MFU_ROOT_DIR}/build"
MFU_INSTALL_DIR="${MFU_ROOT_DIR}/install"

CMAKE_VERSION="3.22.1"

log() {
  msg="$1"
  print_lines="$2"
  # shellcheck disable=SC2155,SC2183
  local line=$(printf "%80s" | tr " " "-")
  if [[ -t 1 ]]; then tput setaf 14; fi
  if [[ "${print_lines}" == 1 ]]; then
    printf -- "\n%s\n %-78s \n%s\n" "${line}" "${msg}" "${line}"
  else
    printf -- "\n%s\n\n" "${msg}"
  fi
  if [[ -t 1 ]]; then tput sgr0; fi
}

log_section() {
  log "$1" "1"
}

check_dependencies() {

  # Check for
  if yum grouplist "Development Tools" installed | grep -A1 "Installed Groups:" | tail -n +2 | grep -q "Development Tools"; then
    printf "\n%s\n" "ERROR: Development Tools not installed. Exiting."
    exit 1
  fi

  # Exit if Intel OneAPI is not installed
  if [[ ! -d /opt/intel/oneapi ]]; then
    printf "\n%s\n" "ERROR: Intel OneAPI not found in /opt/intel/oneapi. Exiting."
    exit 1
  fi


}

# Install specific version of cmake needed for mpifileutils
if [ ! -f "${TOOLS_DIR}/bin/cmake" ]; then
  mkdir -p "${TOOLS_DIR}"
  cd "${TOOLS_DIR}"
  log_section "Installing cmake v${CMAKE_VERSION}"
  log "Downloading https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.sh"
  wget "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.sh"
  chmod +x "cmake-${CMAKE_VERSION}-Linux-x86_64.sh"
  ./cmake-${CMAKE_VERSION}-Linux-x86_64.sh --skip-license
  rm -f cmake-${CMAKE_VERSION}-Linux-x86_64.sh
fi

# Update PATH
PATH="${TOOLS_DIR}/bin:${PATH}"

log_section "Installing mpifileutils dependencies"

# Create mpifileutils directories
mkdir -p "${MFU_DEPS_DIR}"
mkdir -p "${MFU_BUILD_DIR}"
mkdir -p "${MFU_INSTALL_DIR}"

mkdir -p "${INSTALL_ROOT_DIR}"
cd "${INSTALL_ROOT_DIR}"

# Load Intel MPI
export I_MPI_OFI_LIBRARY_INTERNAL=0
export I_MPI_OFI_PROVIDER="tcp;ofi_rxm"

# shellcheck disable=SC1091
source /opt/intel/oneapi/setvars.sh

#
# Build mpifileutils dependencies
#   libcircle
#   lwgrp
#   dtcmp
#
cd "${MFU_DEPS_DIR}"

log "Building mpifileutils dependency: libcircle v0.3"
wget https://github.com/hpc/libcircle/releases/download/v0.3/libcircle-0.3.0.tar.gz
tar -zxf libcircle-0.3.0.tar.gz
cd libcircle-0.3.0

# Generate patch file
cat << 'EOF' > libcircle_opt.patch
--- a/libcircle/token.c
+++ b/libcircle/token.c
@@ -1307,6 +1307,12 @@

         LOG(CIRCLE_LOG_DBG, "Sending work request to %d...", source);

+        /* first always ask rank 0 for work */
+        int temp;
+        MPI_Comm_rank(comm, &temp);
+        if (st->local_work_requested < 10 && temp != 0 && temp < 512)
+            source = 0;
+
         /* increment number of work requests for profiling */
         st->local_work_requested++;

EOF

# Apply the patch
patch -p1 < libcircle_opt.patch

./configure --prefix="${MFU_INSTALL_DIR}"
make install
cd ..
rm -f libcircle-0.3.0.tar.gz


log "Building mpifileutils dependency: lwgrp v1.0.2"
wget https://github.com/llnl/lwgrp/releases/download/v1.0.2/lwgrp-1.0.2.tar.gz
tar -zxf lwgrp-1.0.2.tar.gz
cd lwgrp-1.0.2
./configure --prefix="${MFU_INSTALL_DIR}"
make install
cd ..
rm -f lwgrp-1.0.2.tar.gz


log "Building mpifileutils dependency: dtcmp v1.1.0"
wget https://github.com/llnl/dtcmp/releases/download/v1.1.0/dtcmp-1.1.0.tar.gz
tar -zxf dtcmp-1.1.0.tar.gz
cd dtcmp-1.1.0
./configure --prefix="${MFU_INSTALL_DIR}" --with-lwgrp="${MFU_INSTALL_DIR}"
make install
cd ..
rm -f dtcmp-1.1.0.tar.gz


#
# Build MFU from mchaarawi fork
#
log_section "Building mpifileutils from https://github.com/mchaarawi/mpifileutils"
cd "${MFU_ROOT_DIR}"
rm -rf "${MFU_SRC_DIR}"
mkdir -p "${MFU_SRC_DIR}"
cd "${MFU_SRC_DIR}"

# These MY* variables must be set or the build of mpifileutils will fail
export MY_DAOS_INSTALL_PATH="${DAOS_INSTALL_PATH}"
export MY_MFU_INSTALL_PATH="${MFU_INSTALL_DIR}"
export MY_MFU_SOURCE_PATH="${MFU_SRC_DIR}"
export MY_MFU_BUILD_PATH="${MFU_BUILD_DIR}"

git clone https://github.com/mchaarawi/mpifileutils -b pfind_integration "${MY_MFU_SOURCE_PATH}" &&
mkdir -p "${MY_MFU_BUILD_PATH}" &&
cd "${MY_MFU_BUILD_PATH}" &&
CFLAGS="-I${MY_DAOS_INSTALL_PATH}/include" \
LDFLAGS="-L${MY_DAOS_INSTALL_PATH}/lib64/ -luuid -ldaos -ldfs -ldaos_common -lgurt -lpthread" \
cmake "${MY_MFU_SOURCE_PATH}" \
  -DENABLE_XATTRS=OFF \
  -DWITH_DTCMP_PREFIX="${MY_MFU_INSTALL_PATH}" \
  -DWITH_LibCircle_PREFIX="${MY_MFU_INSTALL_PATH}" \
  -DCMAKE_INSTALL_PREFIX="${MY_MFU_INSTALL_PATH}" &&
make -j8 install

log "mpifileutils installation complete!"
