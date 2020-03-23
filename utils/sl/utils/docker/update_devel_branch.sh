#!/bin/bash

# This script reads in the build.config file and will validate and optionally
# update the git HPDD git repository <component>_<devel> branch for each
# component to match the commit hashes in build.config file.
#
# This procedure should then be used after a build.config update is validated
# to update the branches.
#
# The repositories for the components will be updated in their own
# subdirectory at the appropriate branch, which will usually be master.
#
# ${1} Needs to be "update" for the git branch to be updated in gerrit
# and the user will need push permission to actually do an update.
#

# BUILD_CONFIG    specifies the build.config to use.
#                 Default is the first one found in current directory tree.
#
# JOB_NAME        Specifies the Jenkins job name.  Normally set by Jenkins.
#
# TARGET          specifies what you are getting the dependencies for.
#                 Default is JOB_NAME portion before the first "-" if JOB_NAME
#                 exists.  If JOB_NAME does not exist, TARGET needs to exist.
#
# BRANCH_SUFFIX   Specifies the suffix to used for the branch.
#                 Default is "devel"
#
# SCONS_LOCAL     Specifies where the scons_local directory to use is.
#                 Default is to the use scons_local from the parent directory
#                 of this script.


set -e -x

set +u

job_real_name=""
if [ -n "${JOB_NAME}" ];then
  job_real_name=${JOB_NAME%/*}
  : "${JOB_SUFFIX:="-${job_real_name#*-}"}"
fi

: "${TARGET:="${job_real_name%-*}"}"
: "${JOB_SUFFIX:="-master"}"

: "${DEPEND_INFO:="depend_info"}"

update=""
if [ "${1}" == "update" ]; then
  update="true"
fi

set -u

if [ -n "${DEPEND_INFO}" ]; then
  rm -rf "${DEPEND_INFO}"
fi

: "${BRANCH_SUFFIX:="devel"}"

my_path="$(dirname "${0}")"
my_path_back1="$(dirname "${my_path}")"
my_path_back2="$(dirname "${my_path_back1}")"
my_path_abs="$(readlink -f "${my_path_back2}")"

: "${SCONS_LOCAL:="${my_path_back2}"}"

# For the component repos, there should be a <name>-<name>_<suffix> Jenkins job
# that has as artifacts the git commits wanted by the build.config file.

# Make the internal name shorter.
bsx=${BRANCH_SUFFIX}

repo_base='ssh://review.hpdd.intel.com:29418'

component_script="${PWD}/component_info.sh"

pushd "${SCONS_LOCAL}"
  scons -f "${my_path_abs}/utils/docker/SConstruct_info" \
    --output-script="${component_script}"
popd

# shellcheck disable=SC1090
. "${component_script}"

# Keep public_repo_xxx arrays in same order and count.
public_repo_name=(\
  'argobots' \
  'fuse' \
  'mercury' \
  'pmdk' \
  'ofi' \
  'ompi' \
  'openpa' \
  'isal' \
  'spdk')

public_repo_dir=(\
  'daos/argobots' \
  'coral/libfuse' \
  'daos/mercury' \
  'coral/nvml' \
  'coral/libfabric' \
  'coral/ompi' \
  'daos/openpa' \
  'daos/isal' \
  'daos/spdk')

# Keep comp_repo_xxx arrays in same order and count
comp_repo_name=('cart' 'cppr' 'daos' 'iof')
comp_repo_dir=('daos/cart' 'coral/cppr' 'daos/daos_m' 'daos/iof')

std_repo_name=("${public_repo_name[@]}" "${comp_repo_name[@]}")
std_repo_dir=("${public_repo_dir[@]}" "${comp_repo_dir[@]}")

if [ ! -d "${TARGET}" ]; then
  for i in "${!comp_repo_name[@]}"; do
    if [[ "${TARGET}" = "${comp_repo_name[i]}" ]]; then
      git clone "${repo_base}/${comp_repo_dir[i]}" "${comp_repo_name[i]}"
    fi
  done
fi

pushd "${TARGET}"
  git config advise.detachedHead false
  git clean -dfx
  git reset --hard
  git checkout master
  git fetch origin master
  git reset --hard FETCH_HEAD
  git clean -df
  if [ -d "scons_local" ]; then
    scons_local_commit_raw=$(git submodule status scons_local)
    scons_local_commit1="${scons_local_commit_raw#"-"}"
    scons_local_commit2="${scons_local_commit1#+}"
    scons_local_commit="${scons_local_commit2% *}"
    scons_local_commit="${scons_local_commit% *}"
    slc="${scons_local_commit#"${scons_local_commit%%[![:space:]]*}"}"
    scons_local_commit="${slc}"
    branch_name="${TARGET}_${bsx}"
  fi
popd

# If the repository has a scons_local submodule, then we want to make
# sure that there is a tracking branch created for this.
if [ -n "${scons_local_commit}" ]; then
  pushd "${SCONS_LOCAL}"
    set +e
    repo="${TARGET}"
    my_commit=${scons_local_commit}
    branch_commit="$(git rev-parse "origin/${branch_name}")"
    rc=${?}
    set -e
    if [ ${rc} -eq 0 ]; then
      if [ "${my_commit}" != "${branch_commit}" ]; then
        git branch -d "${branch_name}" || true
        git branch -f "${branch_name}" "${my_commit}"
        if [ -n "${update}" ]; then
          git push -u origin "${branch_name}"
        else
           # Testing / dry-run mode
           echo "would git push -u origin ${branch_name} to ${repo}"
        fi
      fi
    else
      git branch -f "${branch_name}" "${my_commit}"
      if [ -n "${update}" ]; then
        git push -u origin "${branch_name}"
      else
        # Testing / dry-run mode.
        echo "would git push -u origin ${branch_name}"
      fi
    fi
  popd
fi

print_err() { printf "%s\n" "$*" 1>&2; }

def_build_config=$(find . -name 'build.config' -print -quit)
: "${BUILD_CONFIG:="${def_build_config}"}"

set + u
if [ -z "${BUILD_CONFIG}" ]; then
  print_err "A build.config file was not found."
  exit 1
fi

build_config_script="${PWD}/tmp_build_config.sh"
"${SCONS_LOCAL}/utils/get_build_config_info.py" \
  --build-config="${BUILD_CONFIG}" > "${build_config_script}"

config_filekeys=""
old_config_filekeys="<>"

count=0
# shellcheck disable=SC1090
source "${build_config_script}"
while [ "${config_filekeys}" != "${old_config_filekeys}" ]; do

  let count=count+1
  if [ ${count} -gt 5 ]; then
    echo "More than 5 loops.  We know we don't have that many!"
    break;
  fi

  # Skip if no additional config files.
  if [ -z "${config_filekeys}" ]; then
    break;
  fi

  old_config_filekeys=${config_filekeys}

  for filekey in ${config_filekeys}; do
    if [ ! -d "${filekey}" ]; then
      for i in "${!comp_repo_name[@]}"; do
        if [[ "${filekey}" = "${comp_repo_name[i]}" ]]; then
          git clone "${repo_base}/${comp_repo_dir[i]}" "${comp_repo_name[i]}"
        fi
      done
    fi
    commit_sym="${filekey}_git_hash"
    pushd "${filekey}"
      git config advise.detachedHead false
      git clean -dfx
      git reset --hard
      git checkout master
      git fetch origin "${!commit_sym}"
      git reset --hard FETCH_HEAD
      git clean -df
    popd

  done
  old_config_filekeys=${config_filekeys}
  config_filekeys=""
  # shellcheck disable=SC1090
  source "${build_config_script}"
done


# TODO, Need to update the docker/SConstruct_info to extract branch info.
# Not currently needed.
my_branch=""

# Loop through the repos in use.
for i in "${!std_repo_name[@]}"; do
  repo="${std_repo_name[i]}"
  if [ ! -d "${repo}" ]; then
    git clone "${repo_base}/${std_repo_dir[i]}" "${std_repo_name[i]}"
  fi
  set +u
  my_wanted_commit="${repo}_git_hash"
  my_commit="${!my_wanted_commit}"
  if [ -z "${my_commit}" ]; then
    continue
  fi

  branch_name="${TARGET}_${bsx}"

  pushd "${repo}"
    git config advise.detachedHead false
    git clean -dfx
    git reset --hard
    git checkout master
    if [ -n "${my_branch}" ]; then
      git branch -D ${my_branch} || true
      git fetch origin ${my_branch}
      git checkout -b ${my_branch} origin/${my_branch}
    else
      git fetch origin master
      git reset --hard FETCH_HEAD
      git clean -df
    fi
    git fetch --tags
    git checkout -f "${my_commit}"
    git clean -dfx
    set +e
    branch_commit=$(git rev-parse "origin/${branch_name}")
    rc=${?}
    set -e
    if [ ${rc} -eq 0 ]; then
      if [ "${my_commit}" != "${branch_commit}" ]; then
        git branch -d "${branch_name}" || true
        git branch -f "${branch_name}" "${my_commit}"
        if [ -n "${update}" ]; then
          git push -f -u origin "${branch_name}"
        else
          echo "would git push -u origin ${branch_name} to ${repo}"
          git branch -d "${branch_name}" || true
        fi
      fi
    else
      git branch -f "${branch_name}" "${my_commit}"
      if [ -n "${update}" ]; then
        git push -f -u origin "${branch_name}"
      else
        echo "would git push -u origin ${branch_name}"
        git branch -d "${branch_name}" || true
      fi
    fi
    set -e
  popd
  set -u
done
