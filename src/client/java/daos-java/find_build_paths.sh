#!/usr/bin/env bash

daos_install_path=$1
var=$2
in_tree=$3
build_vars=../../../../.build_vars_.sh
build_dir=../../../../../build/
if [ ! -f "${build_vars}" ]
then
  if [ "$var" = "SL_PREFIX" ]
  then
    if [ "$in_tree" = true ]
    then
      echo "${build_dir}/dev/gcc/src/"
    else
      echo "${daos_install_path}"
    fi
  else # SL_PROTOBUFC_PREFIX, default to release
    if [ "$in_tree" = true ]
    then
      echo "${build_dir}/external/dev/protobufc/"
    else
      echo "${daos_install_path}/prereq/release/protobufc/"
    fi
  fi
else
  source "${build_vars}"
  echo "${!var}"
fi
