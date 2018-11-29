#!/bin/bash

# This is a procedure to find the artifacts for the components used to
# build a target.  It attempts to find the specific commit hash if posible.
#
# If the specific commit hash can not be found, a status file named
# "commit_not_found" will be created with the details.
# If all specified commit hashes are found, then the "commit_not_found"
# file will not be present.
#
# This procedure uses environment variables to set the default
# environment for looking up the Jenkins builds.
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
# JOB_SUFFIX      Specifies the suffix for the job type used to look for
#                 dependent jobs to find artifacts in.
#                 If JOB_NAME exists, then it is the JOB_NAME portion after
#                 the first "-".  If JOB_NAME does not exist, defaults to
#                 "-master".
#
# DEPEND_JOBS     The dependendent jobs to look in for the artifacts.
#                 Default is to first look for jobs from the build.config file
#                 with the dependent name and a suffix of:
#                   "_" + TARGET + "_" + JOB_SUFFIX
#                 A review job would set these to be a space delimited list
#                 of the upstream dependent jobs.  eg:
#                 "argobots-cart_devel mpi4py-cart_devel mercury-cart_devel"
#                 "cart-review-child fuse-iof_devel"
#
# DEPEND_COMPS    The dependent components list.  By default this will be
#                 built from the build.config file.  This is used if the
#                 build.config file is missing commit hashes for a component.
#
# WORK_TARGET     The base directory to unpack the tarball artifacts to.
#                 Each artifact will be extracted into its own directory.
#                 Default is "dist_target"
#
# CORAL_ARTIFACTS This is where to get the artifacts from.  This is normally
#                 set by Jenkins.  Defaults to "/scratch/jenkins-2/artifacts"
#
# arch            The target architechure.  This is normally supplied by
#                 Jenkins.  Default is "x86_64" which is all we are building.
#
# distro          The target distro.  This is normally set by Jenkins.
#                 Default is "sles12" if /etc/SuSE-relese is found otherwise
#                 the default is "el7".
#
# DEPEND_RPMS     This is where to place any RPMs found with dependent jobs.
#                 Default is "depend_rpms".
#                 This directory is cleared at the start of the script.
#
# DEPEND_INFO     This is where to place any special information such as
#                 Git information from the dependent jobs.
#                 Default is "depend_info".
#                 This directory is cleared at the start of the script.
#
# DEPEND_TARBALLS This is where to place any tarballs found with dependent
#                 jobs.  Default is "depend_tarballs".
#                 This directory is cleared at the start of the script.
#
# SCONS_LOCAL     Specifies where the scons_local directory to use is.
#                 Default is to the use scons_local from the parent directory
#                 of this script.


set +u -x

job_real_name=""
if [ -n "${JOB_NAME}" ];then
  job_real_name=${JOB_NAME%/*}
  # shellcheck disable=SC2153
  : "${JOB_SUFFIX:="-${job_real_name#*-}"}"
fi

: "${TARGET:="${job_real_name%-*}"}"
: "${JOB_SUFFIX:="-master"}"
job_suffix=${JOB_SUFFIX#-}

: "${DEPEND_JOBS:=""}"

if [ -z "${TARGET}" ]; then
  echo "Either JOB_NAME or TARGET environment variables must exist."
  exit 1
fi

if [ -z "${SCONS_LOCAL}" ]; then
   test_scl="${TARGET}/scons_local"
   if [ ! -e "${test_scl}" ]; then
     test_scl=$(find . -name 'scons_local' -print -quit)
     if [ ! -e "${test_scl}" ]; then
       echo "Unable to find scons_local directory."
       exit 1
     fi
   fi
   SCONS_LOCAL="${test_scl}"
fi

: "${WORK_TARGET:="dist_target"}"

: "${CORAL_ARTIFACTS:="/scratch/jenkins-2/artifacts"}"

# shellcheck disable=SC2154
: "${arch:="x86_64"}"

def_distro="el7"
if [ -e "/etc/SuSE-release" ]; then
  def_distro="sles12"
fi
# shellcheck disable=SC2154
: "${distro="${def_distro}"}"

: "${DEPEND_RPMS:="depend_rpms"}"

: "${DEPEND_INFO:="depend_info"}"

: "${DEPEND_TARBALLS:="depend_tarballs"}"

set -u

def_build_config=$(find . -name 'build.config' -print -quit)
: "${BUILD_CONFIG:="${def_build_config}"}"

set + u
if [ -z "${BUILD_CONFIG}" ]; then
  echo "A build.config file was not found."
  exit 1
fi

if [ -n "${DEPEND_RPMS}" ]; then
  rm -rf "${DEPEND_RPMS}"
  mkdir -p "${DEPEND_RPMS}"
fi

if [ -n "${DEPEND_INFO}" ]; then
  rm -rf "${DEPEND_INFO}"
  mkdir -p "${DEPEND_INFO}"
fi

if [ -n "${DEPEND_TARBALLS}" ]; then
  rm -rf "${DEPEND_TARBALLS}"
  mkdir -p "${DEPEND_TARBALLS}"
fi

function fetch_job_artifacts {
  artifact=${1}

  if [ ! -d "${artifact}" ]; then
    return 0
  fi

  # Copy over any RPMs
  if [ -d "${DEPEND_RPMS}" ];then
    rpm_files=$(find "${artifact}" -name "*.rpm" -print)
    if [ -n "${rpm_files}" ]; then
      cp "${artifact}"/*.rpm "${DEPEND_RPMS}"
    fi
  fi
  # Unpack any tarballs
  tar_files=$(find "${artifact}" -name "*_files.tar.gz" -print)
  mapfile -t tar_file_lines <<< "${tar_files}"
  for tar_file in "${tar_file_lines[@]}"; do
    dest_filename=$(basename "${tar_file}")
    dest_name=${dest_filename%_files.tar.gz}
    if [ ! -d "${WORK_TARGET}/${dest_name}" ]; then
      if [ -e "${tar_file}" ]; then
        mkdir -p "${WORK_TARGET}/${dest_name}"
        tar -C "${WORK_TARGET}/${dest_name}" -xzf "${tar_file}"
        if [ -d "${DEPEND_TARBALLS}" ]; then
          cp "${tar_file}" "${DEPEND_TARBALLS}"
        fi
      fi
      if [ -d "${DEPEND_INFO}" ]; then
        #git_files=$(find "${artifact}" -name "*_git_*" -print)
        #mapfile -t git_file_lines <<< "${git_files}"
        #for git_file in "${get_file_lines[@]}"; do
        git_file="${dest_name}_git_commit"
        git_tag="${dest_name}_git_tag"
        art_tag=""
        if [ -e "${artifact}/${git_tag}" ]; then
          cp "${artifact}/${git_tag}" "${DEPEND_INFO}"
          art_tag=$(cat "${DEPEND_INFO}/${git_tag}")
        fi
        if [ -e "${artifact}/${git_file}" ]; then
          cp "${artifact}/${git_file}" "${DEPEND_INFO}"
          dest_filename=$(basename "${git_file}")
          if [[ $dest_filename == *"_git_commit" ]]; then
            test_git=${dest_name}_git_hash
            set +u
            test_hash=${!test_git}
            set -u
            if [ -n "${test_hash}" ] && [ "${test_hash}" != "latest" ]; then
              art_hash=$(cat "${DEPEND_INFO}/${git_file}")
              if [ "${art_hash}" != "${test_hash}" ]; then
                if [ "${art_tag}" != "${test_hash}" ]; then
                  # mpi4py is using the ompi commit hash for wanted commit
                  # Which will not match any saved mpi4py commit hash.
                  # mpi4py is obtained by the web-retriever, which does not
                  # use a commit hash.
                  if [ "${dest_name}" != "mpi4py" ]; then
                    printf "###\n%s commit should be '%s' was '%s'\n###\n" \
                      "${dest_name}" "${test_hash}" "${art_hash}"
                    printf "%s %s not found\n" "${dest_name}" "${test_hash}" \
                      >> commit_not_found
                  fi
                fi
              fi
            fi
          fi
        fi
        git_branch="${dest_name}_git_branch"
        if [ -e "${artifact}/${git_branch}" ]; then
          cp "${artifact}/${git_branch}" "${DEPEND_INFO}"
        fi
      fi
    fi
  done
}

# Read and parse the git commit hashes from build.config
if [ -e "${SCONS_LOCAL}/utils/get_build_config_info.py" ]; then
  build_config_script="${PWD}/tmp_build_config.sh"
  "${SCONS_LOCAL}/utils/get_build_config_info.py" \
    --build-config="${BUILD_CONFIG}" > "${build_config_script}"

  source "${build_config_script}"

  for depend_name in ${depend_names}; do
    if [ -d "${WORK_TARGET}/${depend_name}" ]; then
      rm -rf "${WORK_TARGET:?}/${depend_name}"
    fi
  done
else
  # scons_local/utils/docker is updated more often
  # than scons_local/utils for some jobs, so needs to be backwards
  # compatible for a short time.
  # Read and parse the git commit hashes from build.config
  gr1='grep -v depends='
  gr2='grep -v component='
  depend_hashes=$(grep -i "\s*=\s*" "${BUILD_CONFIG}" | ${gr1} | ${gr2})
  mapfile -t depend_lines <<< "${depend_hashes}"
  depend_names=""
  for line in "${depend_lines[@]}"; do
    # shellcheck disable=SC1001
    if [[ "${line}" != \#* ]]; then
      if [[ "${line}" != *config ]] ; then
        depend_name=${line%=*}
        depend_name=${depend_name% *}
        depend_name=${depend_name,,}
        depend_hash=${line#*=}
        depend_hash=${depend_hash#* }
        depend_hash=${depend_hash,,}
        declare ${depend_name}_git_hash=${depend_hash}
        depend_names="${depend_names} ${depend_name}"
        if [ -d "${WORK_TARGET}/${depend_name}" ]; then
          rm -rf "${WORK_TARGET:?}/${depend_name}"
        fi
      fi
    fi
  done
fi

# Some special cases needed
if [[ ${depend_names} =~ ompi ]]; then
  # ompi implies mpi4py which is built after ompi and is indexed
  # by the ompi hash
  if [[ ! ${depend_names} =~ mpi4py ]]; then
    depend_names=" mpi4py ${depend_names}"
    # shellcheck disable=SC2034 disable=SC2154
    declare mpi4py_git_hash=${ompi_git_hash}
  fi
fi
if [[ ${depend_names} =~ openpa ]]; then
  # openpa implies mercury which is currently on a unstable topic_ofi branch
  # so we need to specify latest.  This should be looking only at a jenkins
  # building the appropriate commit hash.
  if [[ ! ${depend_names} =~ mercury ]]; then
    depend_names=" mercury ${depend_names}"
    # shellcheck disable=SC2034
    declare mercury_git_hash="latest"
  fi
fi
# bmi not being built so ignore
depend_names=${depend_names// bmi/}
: "${DEPEND_COMPS:="${depend_names}"}"
export DEPEND_COMPS

test_distro="arch=x86_64,distro=el7"
wanted_distro="arch=${arch},distro=${distro}"

rm -f commit_not_found

set +u
# First look to see if specific jobs should be searched
# for artifacts.
if [ -n "${DEPEND_JOBS}" ]; then
  for depend_job in ${DEPEND_JOBS}; do
    test_name="${depend_job%-*}"
    test_job_key=${test_name}_git_hash
    set +u
    wanted_commit=${!test_job_key}
    set -u
    if [ -n "${wanted_commit}" ]; then
      artifact_base="${CORAL_ARTIFACTS}/${depend_job}"
      artifact_test_base="${artifact_base}/${test_distro}"
      artifact_test=${artifact_test_base}/${wanted_commit}
      if [ -d "${artifact_test}" ]; then
        artifact_dir=$(readlink -f "${artifact_test}")
        artifact_no="${artifact_dir##*/}"
        artifact_distro="${artifact_base}/${wanted_distro}"
        artifact="${artifact_distro}/${artifact_no}"
        if [ -n "${artifact_no}" ]; then
          fetch_job_artifacts "${artifact}"
        fi
      fi
    fi
  done
fi

# Did we find all the artifacts?
for test_name in ${DEPEND_COMPS}; do
  test_job_key=${test_name}_git_hash
  set +u
  wanted_commit=${!test_job_key}
  set -u

  if [ -z "${wanted_commit}" ]; then
     echo "Something is broken"
     wanted_commit="latest"
  fi

  artifact_no=""
  if [ ! -d "${WORK_TARGET}/${test_name}" ]; then
    # Look first for $test_name-$job_suffix, then -release, -master
    for x_suffix in ${job_suffix} release master; do
      test_job_name="${test_name}-${x_suffix}"
      artifact_base="${CORAL_ARTIFACTS}/${test_job_name}"
      artifact_test_base="${artifact_base}/${test_distro}"
      artifact_test=${artifact_test_base}/${wanted_commit}
      if [ -d "${artifact_test}" ]; then
        break
      fi
    done
    if [ ! -d "${artifact_test}" ]; then
      if [ "${test_name}" != "mpi4py" ]; then
        printf "%s %s not found\n" "${test_name}" "${wanted_commit}" >> \
          commit_not_found
      fi
      artifact_test="${artifact_test_base}/latest"
    fi
    artifact_dir=$(readlink -f "${artifact_test}")
    artifact_no="${artifact_dir##*/}"
  fi

  artifact_distro="${artifact_base}/${wanted_distro}"
  if [ -n "${artifact_no}" ]; then
    artifact="${artifact_distro}/${artifact_no}"
    fetch_job_artifacts "${artifact}"
  fi
done

