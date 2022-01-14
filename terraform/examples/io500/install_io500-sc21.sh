#!/bin/bash
#
# Install IO500 SC21
#
# This script assumes that
#   1. The intel-oneapi-mpi and intel-oneapi-mpi-devel
#      packages from https://yum.repos.intel.com/oneapi have already been
#      installed
#   2. mpifileutils has already been installed.
#      To install mpifileutils see mfu_install.sh in the same directory as this
#      script.
#

set -e
trap 'echo "An unexpected error occurred. Exiting."' ERR

IO500_VERSION_TAG="io500-sc21"

# Set environment variable defaults if not already set
# This allows for the variables to be set to different values externally.
# Variable names chosen to match instructions at
# https://daosio.atlassian.net/wiki/spaces/DC/pages/11055792129/IO-500+SC21
: "${MY_DAOS_INSTALL_PATH:=/usr}"
: "${MY_MFU_INSTALL_PATH:=/usr/local/mpifileutils/install}"
: "${MY_MFU_SOURCE_PATH:=/usr/local/mpifileutils/src}"
: "${MY_MFU_BUILD_PATH:=/usr/local/mpifileutils/build}"
: "${MY_IO500_PATH:=/usr/local/${IO500_VERSION_TAG}}"

# Environment variables must be exported in order to compile IO500
export MY_DAOS_INSTALL_PATH
export MY_MFU_INSTALL_PATH
export MY_MFU_SOURCE_PATH
export MY_MFU_BUILD_PATH
export MY_IO500_PATH


log() {
  local msg="|  $1  |"
  line=$(printf "${msg}" | sed 's/./-/g')
  # FIX: Can't use tput when running this script with pdsh
  #tput setaf 14 # set Cyan color
  printf -- "\n${line}\n${msg}\n${line}\n"
  #tput sgr0 # reset color
}

log "Installing IO500 ${IO500_VERSION_TAG}"

# Load Intel MPI
export I_MPI_OFI_LIBRARY_INTERNAL=0
export I_MPI_OFI_PROVIDER="tcp;ofi_rxm"
source /opt/intel/oneapi/setvars.sh

export LD_LIBRARY_PATH="${MY_MFU_INSTALL_PATH}/lib":$LD_LIBRARY_PATH
export LD_LIBRARY_PATH="${MY_MFU_INSTALL_PATH}/lib64":$LD_LIBRARY_PATH
export PATH="${MY_MFU_INSTALL_PATH}/bin":$PATH

IO500_INSTALL_PATH=$(dirname $MY_IO500_PATH)
mkdir -p "${IO500_INSTALL_PATH}"
cd "${IO500_INSTALL_PATH}"

log "Cloning https://github.com/IO500/io500 repo. Tag ${IO500_VERSION_TAG}"
if [[ -d "${MY_IO500_PATH}" ]]
then
  rm -rf "${MY_IO500_PATH}"
fi

git clone https://github.com/IO500/io500.git \
  -b ${IO500_VERSION_TAG} \
  "${MY_IO500_PATH}"
cd "${MY_IO500_PATH}"
git checkout -b "${IO500_VERSION_TAG}-daos"

# Need to patch prepare.sh in order to:
#   Point to the pfind that works with our mpifileutils
#   Build ior with DFS support
log "Patching ${MY_IO500_PATH}/prepare.sh"
cd "${MY_IO500_PATH}"
# Attempt to always ensure the patch applies successfully
cp prepare.sh prepare.sh.$(date "+%Y-%m-%d_%H%M%S")
git checkout prepare.sh
cat > io500_prepare.patch <<'EOF'
diff --git a/prepare.sh b/prepare.sh
index f793dfe..d4cb7e8 100755
--- a/prepare.sh
+++ b/prepare.sh
@@ -8,7 +8,7 @@ echo It will output OK at the end if builds succeed
 echo

 IOR_HASH=14deedfec48ce295dff683d15c1b194652bd6d08
-PFIND_HASH=62c3a7e31
+PFIND_HASH=mfu_integration

 INSTALL_DIR=$PWD
 BIN=$INSTALL_DIR/bin
@@ -59,7 +59,7 @@ function get_ior {

 function get_pfind {
   echo "Preparing parallel find"
-  git_co https://github.com/VI4IO/pfind.git pfind $PFIND_HASH
+  git_co https://github.com/mchaarawi/pfind pfind $PFIND_HASH
 }

 function get_schema_tools {
@@ -73,7 +73,7 @@ function build_ior {
   pushd $BUILD/ior
   ./bootstrap
   # Add here extra flags
-  ./configure --prefix=$INSTALL_DIR
+  ./configure --prefix=$INSTALL_DIR --with-daos=${MY_DAOS_INSTALL_PATH}
   cd src
   $MAKE clean
   $MAKE install
EOF

git apply io500_prepare.patch


# Update the Makefile with correct paths
# The Makefile needs to be updated to use the install location of DAOS and MFU.
log "Update ${MY_IO500_PATH}/Makefile with correct paths"

cat > io500_Makefile.patch <<EOF
diff --git a/Makefile b/Makefile
index 2975471..5dce307 100644
--- a/Makefile
+++ b/Makefile
@@ -1,10 +1,13 @@
 CC = mpicc
 CFLAGS += -std=gnu99 -Wall -Wempty-body -Werror -Wstrict-prototypes -Werror=maybe-uninitialized -Warray-bounds
+CFLAGS += -I${MY_DAOS_INSTALL_PATH}/include -I${MY_MFU_INSTALL_PATH}/include

 IORCFLAGS = \$(shell grep CFLAGS ./build/ior/src/build.conf | cut -d "=" -f 2-)
 CFLAGS += -g3 -lefence -I./include/ -I./src/ -I./build/pfind/src/ -I./build/ior/src/
 IORLIBS = \$(shell grep LDFLAGS ./build/ior/src/build.conf | cut -d "=" -f 2-)
 LDFLAGS += -lm \$(IORCFLAGS) \$(IORLIBS) # -lgpfs # may need some additional flags as provided to IOR
+LDFLAGS += -L${MY_DAOS_INSTALL_PATH}/lib64 -ldaos -ldaos_common -ldfs -lgurt -luuid
+LDFLAGS += -L${MY_MFU_INSTALL_PATH}/lib64 -lmfu_dfind -lmfu

 VERSION_GIT=\$(shell git describe --always --abbrev=12)
 VERSION_TREE=\$(shell git diff src | wc -l | sed -e 's/   *//g' -e 's/^0//' | sed "s/\([0-9]\)/-\1/")
EOF

git apply io500_Makefile.patch

# Run prepare.sh
log "Running ${MY_IO500_PATH}/prepare.sh"
${MY_IO500_PATH}/prepare.sh

# Download example config
cd ${MY_IO500_PATH}
wget https://raw.githubusercontent.com/mchaarawi/io500/main/config-full-sc21.ini

printf "\nIO500 ${IO500_VERSION_TAG} installation complete!\n\n"
