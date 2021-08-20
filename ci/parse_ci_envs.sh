#!/bin/bash

# This is a file to be sourced by other files needing to parse
# the jenkins environment variables for a build stage to determine
# what specifically they need to build.
set -uex

: "${DAOS_STACK_LEAP_15_VERSION:=15.3}"
: "${DAOS_STACK_CENTOS_7_VERSION:=7.9}"
: "${DAOS_STACK_CENTOS_8_VERSION:=8}"

: "${DOT_VER:=}"
if [ -n "${STAGE_NAME:?}" ]; then
  case $STAGE_NAME in
    *CentOS\ 7*|*el7*|*centos7*)
      : "${CHROOT_NAME:=epel-7-x86_64}"
      : "${TARGET:=centos7}"
      ;;
    *CentOS\ 8*|*el8*|*centos8*)
      : "${CHROOT_NAME:=epel-8-x86_64}"
      : "${TARGET:=centos8}"
      if [[ "$STAGE_NAME" == *"8.3"* ]]; then
        DOT_VER="3"
      elif [[ "$STAGE_NAME" == *"8.4"* ]]; then
        DOT_VER='4'
      fi
      ;;
    *Leap\ 15*|*leap15*|*opensuse15*|*sles15*)
      if [[ "$STAGE_NAME" == *"15.2"* ]]; then
        : "${CHROOT_NAME:=opensuse-leap-15.2-x86_64}"
        : "${TARGET:=opensuse-15.2}"
      elif [[ "$STAGE_NAME" == *"15.3"* ]]; then
        : "${CHROOT_NAME:=opensuse-leap-15.3-x86_64}"
        : "${TARGET:=opensuse-15.3}"
      fi
      ;;
    *Ubuntu\ 20.04*|*ubuntu2004*)
      : "${CHROOT_NAME:="not_applicable"}"
      : "${TARGET:=ubuntu20}"
      ;;
  esac
fi

export CHROOT_NAME
export TARGET
export DOT_VER
