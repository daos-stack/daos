#!/bin/bash

# This script reads in the build.config file and will validate and optionally
# update the git HPDD git repository <component>_<devel> branch for each
# component to match the commit hashes in build.config file.

# <component>-master are built with the master branch of all components.
#
# <name>-<name>_<devel> is the master branch of <name> that is built with
# the component selected by the commit has from the build.config file.
# The <name>-<name>_<devel> jobs correspond to the "update_scratch" jobs
#
# In general the commit hash will be looked for first in the
# <component>-<name>_<devel> job, and if it is not found there it will
# be looked for in the <component>-<component>_<devel> job.  If there is no
# <component>-<component>_<devel> job then <component>-master is used.
#
# <component>-<name>_<devel> is the <name>_<devel> branch of <component>
# built with the <name>_<devel> branch for the components.  This procedure
# is to make sure that branch is properly set for future builds of those
# components when the build.config is changed.
# The <component>-<name>_<devel> jobs are similar to a specific build of
# an update_scratch job that is referenced in a build.config file.
#
# <component>-<component>_<devel> is the same as <name>-<name>_<devel>.
#
# A successful build for each <component> job with the commit hashes
# is needed to be found for the branch to be updated.
#
# For example, iof-iof_devel is a build of the master branch of iof using
# the components specified by its build.config.
#
# Those components normally will come from cart-iof_devel and fuse-iof-devel
# which are built from the iof_devel branch of the cart and nvml repositories.
#
# In the case that the build.config has been recently updated to specify a
# newer build of cart, then the cart-cart_devel job would be used as a source.
#
# In the case that the build.config has been recently updated to specify a
# newer build of fuse, then since fuse does not have any components,
# fuse-master would be used for the components.
#
# This procedure should then be used after a build.config update is validated
# to update the branches.
#
# The repositories for the components will be updated in their own
# subdirectory at the appropriate branch, which will usually be master.
#
# This script uses the fetch_dependent_artifacts.sh script to obtain and
# validate the git commits in the file.
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
# BRANCH_SUFFIX   Specifies the suffix to used for the branch and component
#                 Jenkins jobs.  Default is "devel"
#
# SCONS_LOCAL     Specifies where the scons_local directory to use is.
#                 Default is to the use scons_local from the parent directory
#                 of this script.


set -e -x

set +u

job_real_name=""
if [ -n "${JOB_NAME}" ];then
  job_real_name=${JOB_NAME%/*}
  : ${JOB_SUFFIX:="-${job_real_name#*-}"}
fi

: ${TARGET:="${job_real_name%-*}"}
: ${JOB_SUFFIX:="-master"}
job_suffix=${JOB_SUFFIX#-}

: ${DEPEND_INFO:="depend_info"}

update=""
if [ "${1}" == "update" ]; then
  update="true"
fi

set -u

if [ -n "${DEPEND_INFO}" ]; then
  rm -rf "${DEPEND_INFO}"
fi

: ${BRANCH_SUFFIX:="devel"}

my_path="`dirname \"${0}\"`"
my_path_back1="`dirname \"${my_path}\"`"
my_path_back2="`dirname \"${my_path_back1}\"`"
my_path_abs="`readlink -f \"${my_path_back2}\"`"

: ${SCONS_LOCAL:="${my_path_back2}"}

# For the component repos, there should be a <name>-<name>_<suffix> Jenkins job
# that has as artifacts the git commits wanted by the build.config file.

# Make the internal name shorter.
bsx=${BRANCH_SUFFIX}

repo_base='ssh://review.whamcloud.com:29418'

component_script="${PWD}/component_info.sh"

pushd ${SCONS_LOCAL}
  scons -f ${my_path_abs}/utils/docker/SConstruct_info \
    --output-script=${component_script}
popd

. ${component_script}

# Still need some special logic to look up the Jenkins jobs.
# cppr has to inherit cart from iof, so iof_devel must include cart_devel.
# daos can use a different cart than iof, so uses daos_devel and cart_devel.

cart_depend_jobs=""
cart_d_depend_jobs=""
for rq in ${cart_requires_full}; do
  cart_depend_jobs="${cart_depend_jobs} ${rq}-release"
  cart_depend_jobs="${cart_depend_jobs} ${rq}-master"
  cart_depend_jobs="${cart_depend_jobs} ${rq}-cart_${bsx}"
  cart_d_depend_jobs="${cart_d_depend_jobs} ${rq}-daos_${bsx}"
done

iof_depend_jobs=""
for rq in ${iof_requires}; do
  iof_depend_jobs="${iof_depend_jobs} ${rq}-release"
  iof_depend_jobs="${iof_depend_jobs} ${rq}-master"
  iof_depend_jobs="${iof_depend_jobs} ${rq}-iof_${bsx}"
done
iof_depend_jobs="${iof_depend_jobs} cart-cart_${bsx} ${cart_depend_jobs}"

cppr_depend_jobs=""
for rq in ${cppr_requires}; do
  cppr_depend_jobs="${cppr_depend_jobs} ${rq}-cppr_${bsx}"
done
cppr_depend_jobs="${cppr_depend_jobs} iof-iof_${bsx} ${iof_depend_jobs}"

daos_depend_jobs=""
for rq in ${daos_requires}; do
  daos_depend_jobs="${daos_depend_jobs} ${rq}-release"
  daos_depend_jobs="${daos_depend_jobs} ${rq}-master"
  daos_depend_jobs="${daos_depend_jobs} ${rq}-daos_${bsx}"
done
daos_depend_jobs="${daos_depend_jobs} cart-cart_${bsx} ${cart_d_depend_jobs}"

wanted_depend_jobs_name="${TARGET}_depend_jobs"

: ${DEPEND_JOBS:="${!wanted_depend_jobs_name}"}

# Keep public_repo_xxx arrays in same order and count.
public_repo_name=(\
  'argobots' \
  'cci' \
  'fuse' \
  'mercury' \
  'nvml' \
  'ofi' \
  'ompi' \
  'openpa' \
  'pmix')

public_repo_dir=(\
  'daos/argobots' \
  'daos/cci' \
  'coral/libfuse' \
  'daos/mercury' \
  'coral/nvml' \
  'coral/libfabric' \
  'coral/ompi' \
  'daos/openpa' \
  'coral/pmix')

# Keep comp_repo_xxx arrays in same order and count
comp_repo_name=('cart' 'cppr' 'daos' 'iof')
comp_repo_dir=('daos/cart' 'coral/cppr' 'daos/daos_m' 'daos/iof')

std_repo_name=("${public_repo_name[@]}" "${comp_repo_name[@]}")
std_repo_dir=("${public_repo_dir[@]}" "${comp_repo_dir[@]}")

# repo_base='ssh://review.whamcloud.com:29418'

for i in "${!comp_repo_name[@]}"; do
  if [[ "${TARGET}" = "${comp_repo_name[i]}" ]]; then
    if [ ! -d ${TARGET} ]; then
      git clone ${repo_base}/${comp_repo_dir[i]} ${comp_repo_name[i]}
    fi
  fi
done

case ${TARGET} in
  daos)
    for depend in ${cart_requires_full}; do
      declare ${depend}_branch=daos_${bsx}
    done
    declare nvml_branch=daos_${bsx}
    ;;
  *)
    wanted_rqs_name="${TARGET}_requires"
    for depend in ${!wanted_rqs_name}; do
      declare ${depend}_branch=${TARGET}_${bsx}
    done
    ;;
esac

pushd ${TARGET}
  git config advise.detachedHead false
  git clean -dfx
  git reset --hard
  git checkout master
  git fetch origin master
  git reset --hard FETCH_HEAD
  git clean -df
popd

. ${my_path_abs}/utils/docker/fetch_dependent_artifacts.sh


print_err() { printf "%s\n" "$*" 1>&2; }

if [ -e commit_not_found ]; then
  print_err "A build for one or more required commit hashes was not found."
  exit 1
fi

# Loop through the repos in use.
for i in "${!std_repo_name[@]}"; do
  repo="${std_repo_name[i]}"
  if [ -e "${DEPEND_INFO}/${repo}_git_commit" ]; then
    my_branch=""
    if [ -e "${DEPEND_INFO}/${repo}_git_branch" ]; then
      my_branch=`cat ${DEPEND_INFO}/${repo}_git_branch`
    fi
    if [ ! -d ${repo} ]; then
      git clone ${repo_base}/${std_repo_dir[i]} ${std_repo_name[i]}
      if [ -n "${my_branch}" ]; then
        pushd ${repo}
          git checkout ${my_branch}
        popd
      fi
    fi
    my_commit="`cat ${DEPEND_INFO}/${repo}_git_commit`"
    set +u
    my_wanted_branch="${repo}_branch"
    branch_name="${!my_wanted_branch}"
    if [ -n "${branch_name}" ]; then
      set -u
      pushd ${repo}
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
        git checkout -f "${my_commit}"
        git clean -dfx
        set +e
        branch_commit=`git rev-parse origin/${branch_name}`
        rc=${?}
        set -e
        if [ ${rc} -eq 0 ]; then
          if [ "${my_commit}" != "${branch_commit}" ]; then
            git branch -d ${branch_name} || true
            git branch -f ${branch_name} ${my_commit}
            if [ -n "${update}" ]; then
              git push -u origin ${branch_name}
            else
              echo "would git push -u origin ${branch_name} to ${repo}"
              git branch -d ${branch_name} || true
            fi
          fi
        else
          git branch -f ${branch_name} ${my_commit}
          if [ -n "${update}" ]; then
            git push -u origin ${branch_name}
          else
            echo "would git push -u origin ${branch_name}"
            git branch -d ${branch_name} || true
          fi
        fi
        set -e
      popd
    fi
    set -u
  fi
done
