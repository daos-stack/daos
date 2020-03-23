#!/bin/bash

set -x
cd /work

function print_status()
{
    echo "*******************************************************************"
    echo "$@"
    echo "*******************************************************************"
}

docker_setup_file="/work/docker_setup.sh"
if [ -e ${docker_setup_file} ]; then
  # shellcheck disable=SC1090
  source ${docker_setup_file}
fi

kw_build=0
: "${KWINJECT_OUT:="/work/kwinject.out"}"
: "${KW_TABLES:="/work/kwtables"}"
if [ -n "${KLOCWORK_PROJECT}" ]; then
  if [ -e "${KW_PATH}/bin/kwinject" ]; then
    kw_build=1
  else
    print_status "kwinject expected, but not found in ${KW_PATH}/bin."
  fi
fi

if [ -e SConstruct ];then
  scons_local_dir="."
else
  scons_local_dir="scons_local"
fi

set -e
pushd ${scons_local_dir}
if [ ${kw_build} -eq 1 ];
then
  # shellcheck disable=SC2086 disable=SC2048 disable=SC2154
  kwinject -o "${KWINJECT_OUT}" scons ${option} $*
else
  # shellcheck disable=SC2086 disable=SC2048 disable=SC2154
  scons ${option} $*
fi

if [ -n "${SCONS_INSTALL}" ];then
  scons install
fi
popd

if [ -n "${CUSTOM_BUILD_STEP}" ];then
  if [ -e "${CUSTOM_BUILD_STEP}" ]; then
    print_status "Running custom build step"
    # shellcheck disable=SC1090
    source "${CUSTOM_BUILD_STEP}"
  else
    print_status "Custom build step file not found!"
  fi
fi

# It is possible that this will not be used for the kwbuildproject step.
if [ -n "${KLOCWORK_URL}" ]; then
  if [ -n "${KLOCWORK_PROJECT}" ]; then
    if [ -e "${KW_PATH}/bin/kwbuildproject" ]; then
      mkdir -p "${KW_TABLES}"
      kwbuildproject --force --verbose \
        --url "${KLOCWORK_URL}/${KLOCWORK_PROJECT}" \
        --tables-directory "${KW_TABLES}" "${KWINJECT_OUT}"
    fi
  fi
fi

