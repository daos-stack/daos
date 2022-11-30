#!/usr/bin/env bash

daos_install_path=$1
var=$2
build_vars=../../../../.build_vars.sh
if [ ! -f "${build_vars}" ]
then
  echo "${daos_install_path}"
else
  source "${build_vars}"
  echo "${!var}"
fi
