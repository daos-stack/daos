#!/usr/bin/env bash

daos_install_path=$1
var=$2
build_vars=../../../../.build_vars.sh
if [ ! -f "${build_vars}" ]
then
  if [ "$var" = "SL_PREFIX" ]
  then
    echo "${daos_install_path}"
  else # SL_PROTOBUFC_PREFIX, default to release
    echo "${daos_install_path}/prereq/release/protobufc/"
  fi
else
  source "${build_vars}"
  echo "${!var}"
fi
