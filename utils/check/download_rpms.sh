#!/bin/bash

set -uex

# This is a script to download RPM artifacts of DAOS from a jenkins system.
# It may need to be edited for other build systems.
# $1 is the branch to build.  Default is master.
#    Release x.y can be specified by x.y for convenience.

: "${WORKSPACE:="${PWD}"}"

: "${RPM_DIR:="${WORKSPACE}/rpm_dir"}"

: "${JENKINS_URL:="https://build.hpdd.intel.com/"}"
job_url="${JENKINS_URL}job/daos-stack/job"

if [ $# -ge 1 ]; then
  if [[ ${1} == [[:digit:]]* ]]; then
    my_branch="release%252F${1}"
  else
    my_branch="${1}"
  fi
else
  my_branch="master"
fi

if [ $# -ge 2 ]; then
  my_build="$2"
else
  my_build="lastSuccessfulBuild"
fi

my_project="daos"

my_art_dir="artifact/artifacts"
my_distro="el8"
my_artifact="${my_distro}.zip"
my_prefix="${my_branch}_"

job_url+="/${my_project}/job/${my_branch}/${my_build}/${my_art_dir}/"
job_url+="${my_distro}/*zip*/${my_artifact}"

# Get the RPM Artifacts
sudo yum -y install curl unzip hardening-check
rm -f "${my_prefix}${my_project}_${my_artifact}"
curl --silent --show-error "${job_url}" \
     -o "${my_prefix}${my_project}_${my_artifact}"

upload=0
if [ -e "${my_prefix}${my_project}_${my_artifact}_last" ]; then
  if ! cmp "${my_prefix}${my_project}_${my_artifact}" \
           "${my_prefix}${my_project}_${my_artifact}_last"; then
    upload=1
  fi
else
  upload=1
fi

rm -rf "${RPM_DIR:?}"

if [ $upload -ne 0 ]; then
  rm -rf tmp_dir
  mkdir -p tmp_dir
  pushd tmp_dir
    unzip "../${my_prefix}${my_project}_${my_artifact}"
    mkdir -p "${RPM_DIR}"
    mv "${my_distro}"/*.x86_64.rpm "${RPM_DIR}"
  popd
  rm -rf tmp_dir
fi

