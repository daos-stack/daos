#!/bin/bash

JOBS="argobots nvml daos cppr iof mcl mercury ompi cart"
if [ $# -ne 1 ]; then
  cat << EOF
Usage: $0 <path_to_duplicate>

This script replicates the Jenkins build artifacts required to build projects
based on HPDD scons_local locally.   The <path_to_duplicate> corresponds to
$CORAL_ARTIFACTS and must exist locally.  It is recommended to use a partition
with plenty of space to store several builds of various components.  Creating
a symbolic link in lieu of a real directory has not been tested.
EOF
  exit 1
fi
PATH_TO_DUPLICATE=$1

touch ${PATH_TO_DUPLICATE}/test

if [ $? -ne 0 ]; then
  cat << EOF
Please setup ${PATH_TO_DUPLICATE} for writing.  It is recommended that you
mount it on a local drive with plenty of space.  For this reason, the script
doesn't create the directory on your behalf
EOF
  exit 1
fi

function copy_latest() {
  job=$1
  scratch_path=${PATH_TO_DUPLICATE}/${job}-update-scratch
  latest=${scratch_path}/latest
  resolved_path=`ssh wolf.hpdd.intel.com readlink -f ${latest}`
  if [ $? -eq 0 ]; then
    if [ ! -d $resolved_path ]; then
      #local path doesn't exist
      mkdir -p ${scratch_path}
      rsync -avz wolf.hpdd.intel.com:${resolved_path} ${scratch_path}
    fi
    build_number=$(basename ${resolved_path})
    echo "Create link from ${latest} to ${build_number}"
    ln -sfn ${build_number} ${latest}
  fi

  if [[ "$job" =~ (cppr|iof|daos|mcl|cart)$ ]]; then
    build_vars=
    for subdir in /$job/TESTING/ / /$job/; do
      vars_path=${resolved_path}${subdir}.build_vars.sh
      if [ -f $vars_path ]; then
        build_vars=$vars_path
        break
      fi
    done

    if [ -n "${build_vars}" ]; then
      echo "Pulling versions from ${build_vars}"
      . ${build_vars}
      for dep in $JOBS; do
        varname=SL_${dep^^}_PREFIX
        if [ -n "${!varname}" ] && [ ! -d ${!varname} ]; then
          #the variable is set and the directory doesn't exist
          dep_path=${!varname}
          base=$(basename $dep_path)
          while [[ $base != *[[:digit:]]* ]]; do
            dep_path=$(dirname $dep_path)
            base=$(basename $dep_path)
          done
          target=$(dirname ${dep_path})
          echo "Copying wolf:$dep_path to ${target}"
          rsync -avz wolf.hpdd.intel.com:${dep_path} ${target}
        fi
        unset ${varname}
      done
    fi
  fi
}

for myjob in $JOBS; do
  echo "Getting latest bits for $myjob"
  copy_latest $myjob
done
