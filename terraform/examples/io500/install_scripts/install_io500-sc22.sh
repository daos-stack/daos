#!/usr/bin/env bash
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
# Install IO500 SC22
#
# Instructions that were referenced to create this script are at
# https://daosio.atlassian.net/wiki/spaces/DC/pages/11167301633/IO-500+SC22
#
# This script assumes that the intel-oneapi-mpi and intel-oneapi-mpi-devel
# packages from https://yum.repos.intel.com/oneapi have already been installed.
# See install_intel-oneapi.sh in the same directory as this script.
#

set -eo pipefail
trap 'echo "An unexpected error occurred. Exiting."' ERR

export IO500_VERSION_TAG="io500-sc22"

# The following variable names match the instructions at
# https://daosio.atlassian.net/wiki/spaces/DC/pages/11167301633/IO-500+SC22
MY_DAOS_INSTALL_PATH="${MY_DAOS_INSTALL_PATH:-/usr}"
MY_IO500_PATH="${MY_IO500_PATH:-/opt/${IO500_VERSION_TAG}}"

# BEGIN: Logging variables and functions
declare -A LOG_LEVELS=([DEBUG]=0 [INFO]=1  [WARN]=2   [ERROR]=3 [FATAL]=4 [OFF]=5)
declare -A LOG_COLORS=([DEBUG]=2 [INFO]=12 [WARN]=3 [ERROR]=1 [FATAL]=9 [OFF]=0 [OTHER]=15)
LOG_LEVEL=INFO

log() {
  local msg="$1"
  local lvl=${2:-INFO}
  if [[ ${LOG_LEVELS[$LOG_LEVEL]} -le ${LOG_LEVELS[$lvl]} ]]; then
    if [[ -t 1 ]]; then tput setaf "${LOG_COLORS[$lvl]}"; fi
    printf "[%-5s] %s\n" "$lvl" "${msg}" 1>&2
    if [[ -t 1 ]]; then tput sgr0; fi
  fi
}

log.debug() { log "${1}" "DEBUG" ; }
log.info()  { log "${1}" "INFO"  ; }
log.warn()  { log "${1}" "WARN"  ; }
log.error() { log "${1}" "ERROR" ; }
log.fatal() { log "${1}" "FATAL" ; }
# END: Logging variables and functions


load_oneapi() {
  if [[ ! -d /opt/intel/oneapi/ ]]; then
    log.error "Intel OneAPI is not installed. Exiting."
    exit 1
  fi

  # Load Intel MPI
  export I_MPI_OFI_LIBRARY_INTERNAL=0
  export I_MPI_OFI_PROVIDER="tcp;ofi_rxm"

  # shellcheck disable=SC1091
  source /opt/intel/oneapi/setvars.sh
}


clone_io500_repo() {
  log.info "Cloning repo: https://github.com/IO500/io500.git,${IO500_VERSION_TAG}"
  git clone https://github.com/IO500/io500.git -b "${IO500_VERSION_TAG}" "${MY_IO500_PATH}"
}

patch_prepare_script() {
  cd "${MY_IO500_PATH}"
  log.info "Patching ${MY_IO500_PATH}/prepare.sh"
  cat > io500_prepare.patch <<EOF
diff --git a/prepare.sh b/prepare.sh
index e38cae6..54dbba5 100755
--- a/prepare.sh
+++ b/prepare.sh
@@ -8,7 +8,7 @@ echo It will output OK at the end if builds succeed
 echo

 IOR_HASH=06fc08e147600f4e5896a5b9b2bf8f1c4a79121f
-PFIND_HASH=62c3a7e31
+PFIND_HASH=dfs_find

 INSTALL_DIR=\$PWD
 BIN=\$INSTALL_DIR/bin
@@ -59,7 +59,7 @@ function get_ior {

 function get_pfind {
   echo "Preparing parallel find"
-  git_co https://github.com/VI4IO/pfind.git pfind \$PFIND_HASH
+  git_co https://github.com/mchaarawi/pfind pfind \$PFIND_HASH
 }

 function get_schema_tools {
@@ -73,7 +73,7 @@ function build_ior {
   pushd "\$BUILD"/ior
   ./bootstrap
   # Add here extra flags
-  ./configure --prefix="\$INSTALL_DIR"
+  ./configure --prefix="\$INSTALL_DIR" --with-daos=\${MY_DAOS_INSTALL_PATH}
   cd src
   \$MAKE clean
   \$MAKE install
EOF

  git apply io500_prepare.patch
}

update_makefile() {
  cd "${MY_IO500_PATH}"
  log.info "Updating ${MY_IO500_PATH}/Makefile with correct paths"
  cat > io500_Makefile.patch <<EOF
diff --git a/Makefile b/Makefile
index ee5cee9..d8fc0e4 100644
--- a/Makefile
+++ b/Makefile
@@ -1,10 +1,12 @@
 CC = mpicc
 CFLAGS += -std=gnu99 -Wall -Wempty-body -Werror -Wstrict-prototypes -Werror=maybe-uninitialized -Warray-bounds
+CFLAGS += -I\${MY_DAOS_INSTALL_PATH}/include

 IORCFLAGS = \$(shell grep CFLAGS ./build/ior/src/build.conf | cut -d "=" -f 2-)
 CFLAGS += -g3 -lefence -I./include/ -I./src/ -I./build/pfind/src/ -I./build/ior/src/
 IORLIBS = \$(shell grep LDFLAGS ./build/ior/src/build.conf | cut -d "=" -f 2-)
 LDFLAGS += -lm \$(IORCFLAGS) \$(IORLIBS) # -lgpfs # may need some additional flags as provided to IOR
+LDFLAGS += -L\${MY_DAOS_INSTALL_PATH}/lib64 -ldaos -ldaos_common -ldfs -lgurt -luuid

 VERSION_GIT=\$(shell git describe --always --abbrev=12)
 VERSION_TREE=\$(shell git diff src | wc -l | sed -e 's/   *//g' -e 's/^0//' | sed "s/\([0-9]\)/-\1/")
EOF

  git apply io500_Makefile.patch
}

main() {
  load_oneapi
  clone_io500_repo
  patch_prepare_script
  update_makefile
  log.info "Run ${MY_IO500_PATH}/prepare.sh"
  "${MY_IO500_PATH}/prepare.sh"
}

main "$@"
