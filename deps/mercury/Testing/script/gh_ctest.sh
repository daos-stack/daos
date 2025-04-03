#!/bin/bash

set -e

if [ -z "$1" ]
then
  echo "Error: no step passed"
  exit 1
fi

CTEST=ctest
CTEST_SCRIPT=Testing/script/gh_script.cmake
STEP=$1

# workaround https://github.com/Homebrew/homebrew-core/issues/158759
if [[ ${RUNNER_OS} == 'macOS' ]]; then
  export CURL_SSL_BACKEND="SecureTransport"
fi

if [[ ${GITHUB_REF}  == 'refs/heads/master' ]] && [[ ${GITHUB_EVENT_NAME} == 'push' ]]; then
  DASHBOARD_MODEL="Continuous"
else
  DASHBOARD_MODEL="Experimental"
fi

if [[ ${GITHUB_REPOSITORY} == 'mercury-hpc/mercury' ]]; then
  DASHBOARD_SUBMIT=TRUE
else
  DASHBOARD_SUBMIT=FALSE
fi

if [[ ${MERCURY_LIBS} == 'static' ]]; then
  BUILD_SHARED=FALSE
else
  BUILD_SHARED=TRUE
fi

if [[ ${MERCURY_PLUGINS} == 'dynamic_plugins' ]]; then
  BUILD_DYNAMIC_PLUGINS=TRUE
else
  BUILD_DYNAMIC_PLUGINS=FALSE
fi

# Source intel env when using icx
if [[ ${CC} == 'icx' ]]; then
  ICX_LATEST_VERSION=$(ls -1 /opt/intel/oneapi/compiler/ | grep -v latest | sort | tail -1)
  source /opt/intel/oneapi/compiler/"$ICX_LATEST_VERSION"/env/vars.sh

  IMPI_LATEST_VERSION=$(ls -1 /opt/intel/oneapi/mpi/ | grep -v latest | sort | tail -1)
  source /opt/intel/oneapi/mpi/"$IMPI_LATEST_VERSION"/env/vars.sh
fi

export COV=`which gcov`

export DEPS_PREFIX=${RUNNER_TEMP}/${INSTALL_DIR}
export PATH=$DEPS_PREFIX/bin:$PATH
export LD_LIBRARY_PATH=$DEPS_PREFIX/lib:$DEPS_PREFIX/lib64:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=$DEPS_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH

export PSM_DEVICES="self,shm"

$CTEST -VV --output-on-failure                        \
  -Ddashboard_full=FALSE -Ddashboard_do_${STEP}=TRUE  \
  -Ddashboard_model=${DASHBOARD_MODEL}                \
  -Dbuild_shared_libs=${BUILD_SHARED}                 \
  -Dbuild_dynamic_plugins=${BUILD_DYNAMIC_PLUGINS}    \
  -Ddashboard_do_submit=${DASHBOARD_SUBMIT}           \
  -Ddashboard_allow_errors=TRUE                       \
  -S $CTEST_SCRIPT
