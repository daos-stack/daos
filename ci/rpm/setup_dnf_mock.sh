#!/bin/bash
# File: setup_dnf_mock.sh

# This sets up a system, presumably Fedora for doing mock builds of RPMS.
# After running this script the first time on a system, the user
# may need to log off and log in again as they need to be a member of
# the mock group.

# This script needs to be run with the source command

sudo dnf -y install createrepo curl git make mock python-srpm-macros \
                    redhat-lsb-core rpmdevtools rpmlint

grep use_nspawn /etc/mock/site-defaults.cfg || \
  echo "config_opts['use_nspawn'] = False" |   \
    sudo tee -a /etc/mock/site-defaults.cf

sudo chmod g+w /etc/mock/*

if id -nGz "${USER}" | grep -qzxF "mock"; then
  echo "Already a member of mock group"
else
  sudo usermod -a -G mock $USER
  echo "You must log out and back in to use mock"
fi

: "${STAGE_NAME:=Build RPM on CentOS 7}"
#: "${STAGE_NAME:=Build RPM on Leap 15}"
: "${SCONS_FAULTS_ARGS:=}"
: "${DAOS_EMAIL:=nobody@example.com}"
: "${DAOS_FULLNAME:=$USER}"
: "${SCONS_FAULTS_ARGS:=BUILD_TYPE=dev}"
export STAGE_NAME
export SCONS_FAULTS_ARGS
export DAOS_EMAIL
export DAOS_FULLNAME
export SCONS_FAULTS_ARGS

el7_group="repository/daos-stack-external-el-7-x86_64-stable-group"
el7_local="repository/daos-stack-el-7-x86_64-stable-local"
el8_group="repository/daos-stack-external-el-8-x86_64-stable-group"
el8_local="repository/daos-stack-el-8-x86_64-stable-local"
leap15_group="repository/daos-stack-external-leap-15-x86_64-stable-group"
leap15_local="repository/daos-stack-leap-15-x86_64-stable-local"
tools_repo="repository/daos-stack-external-stable-local"
repo_url="https://repo.dc.hpdd.intel.com/"
: "${DAOS_STACK_EL_7_GROUP_REPO:=$el7_group}"
: "${DAOS_STACK_EL_7_LOCAL_REPO:=$el7_local}"
: "${DAOS_STACK_EL_8_GROUP_REPO:=$el8_group}"
: "${DAOS_STACK_EL_8_LOCAL_REPO:=$el8_local}"
: "${DAOS_STACK_LEAP_15_GROUP_REPO:=$leap15_group}"
: "${DAOS_STACK_LEAP_15_LOCAL_REPO:=$leap15_local}"
: "${DAOS_STACK_TOOLS_REPO:=$tools_repo}"
: "${REPOSITORY_URL:=$repo_url}"

export DAOS_STACK_EL_7_GROUP_REPO
export DAOS_STACK_EL_7_LOCAL_REPO
export DAOS_STACK_EL_8_GROUP_REPO
export DAOS_STACK_EL_8_LOCAL_REPO
export DAOS_STACK_LEAP_15_GROUP_REPO
export DAOS_STACK_LEAP_15_LOCAL_REPO
export DAOS_STACK_TOOLS_REPO
export REPOSITORY_URL

