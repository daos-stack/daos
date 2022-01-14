#!/bin/bash
#
# Install mpifileutils
#
# This script assumes that the intel-oneapi-mpi and intel-oneapi-mpi-devel
# packages from https://yum.repos.intel.com/oneapi have already been installed
# on the system.
#

set -e
trap 'echo "An unexpected error occurred. Exiting."' ERR

# Set environment variable defaults if not already set
: "${IO500_INSTALL_ROOT_DIR:=/usr/local}"
: "${TOOLS_DIR=${IO500_INSTALL_ROOT_DIR}/tools}"
: "${DAOS_INSTALL_PATH:=/usr}"

# MPI File Utils directories
MFU_ROOT_DIR="${IO500_INSTALL_ROOT_DIR}/mpifileutils"
MFU_DEPS_DIR="${MFU_ROOT_DIR}/deps"
MFU_SRC_DIR="${MFU_ROOT_DIR}/src"
MFU_BUILD_DIR="${MFU_ROOT_DIR}/build"
MFU_INSTALL_DIR="${MFU_ROOT_DIR}/install"

CMAKE_VERSION="3.22.1"


log() {
  local msg="|  $1  |"
  line=$(printf "${msg}" | sed 's/./-/g')
  # FIX: Can't use tput when running this script with pdsh
  #tput setaf 14 # set Cyan color
  printf -- "\n${line}\n${msg}\n${line}\n"
  #tput sgr0 # reset color
}


log "Installing mpifileutils"

# Exit if Intel OneAPI is not installed
if [ ! -d /opt/intel/oneapi ];then
  printf "\nERROR: Intel OneAPI not found in /opt/intel/oneapi. Exiting."
  exit 1
fi

# Install packages needed to build mpifileutils and run IO500
log "Installing Development Tools"
yum group install -y "Development Tools"

log "Installing additional packages"
yum -y install bzip2-devel libarchive-devel openssl-devel git clustershell jq

mkdir -p "${IO500_INSTALL_ROOT_DIR}"
mkdir -p "${TOOLS_DIR}"
cd "${TOOLS_DIR}"

# Install cmake
if [ ! -f "${TOOLS_DIR}/bin/cmake" ];then
log "Installing cmake v${CMAKE_VERSION}"
log "Downloading https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.sh"
wget "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.sh"
chmod +x cmake-${CMAKE_VERSION}-Linux-x86_64.sh
./cmake-${CMAKE_VERSION}-Linux-x86_64.sh --skip-license
rm -f cmake-${CMAKE_VERSION}-Linux-x86_64.sh
fi

cd "${IO500_INSTALL_ROOT_DIR}"

# Update PATH
PATH="${TOOLS_DIR}/bin:${PATH}"

# Load Intel MPI
export I_MPI_OFI_LIBRARY_INTERNAL=0
export I_MPI_OFI_PROVIDER="tcp;ofi_rxm"
source /opt/intel/oneapi/setvars.sh

# Create mpifileutils directories
mkdir -p "${MFU_DEPS_DIR}"
mkdir -p "${MFU_BUILD_DIR}"
mkdir -p "${MFU_INSTALL_DIR}"

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
log "Building mpifileutils from https://github.com/mchaarawi/mpifileutils"
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
  -DWITH_DTCMP_PREFIX=${MY_MFU_INSTALL_PATH} \
  -DWITH_LibCircle_PREFIX=${MY_MFU_INSTALL_PATH} \
  -DCMAKE_INSTALL_PREFIX=${MY_MFU_INSTALL_PATH} &&
make -j8 install

printf "\nmpifileutils installation complete!\n\n"
