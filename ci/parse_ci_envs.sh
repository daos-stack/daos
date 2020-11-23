#!/bin/bash

# This is a file to be sourced by other files needing to parse
# the jenkins environment variables for a build stage to determine
# what specifically they need to build.
set -uex

if [ -n "${STAGE_NAME:?}" ]; then
  case $STAGE_NAME in
    *CentOS\ 7*|*el7*|*centos7*)
      : "${CHROOT_NAME:=epel-7-x86_64}"
      : "${TARGET:=centos7}"
      ;;
    *CentOS\ 8*|*el8*|*centos8*)
      : "${CHROOT_NAME:=epel-8-x86_64}"
      : "${TARGET:=centos8}"
      ;;
    *Leap\ 15*|*leap15*|*opensuse15*|*sles15*)
      : "${CHROOT_NAME:=opensuse-leap-15.2-x86_64}"
      : "${TARGET:=leap15}"
      ;;
    *Ubuntu\ 20.04*|*ubuntu2004*)
      : "${CHROOT_NAME:="not_applicable"}"
      : "${TARGET:=ubuntu20}"
      ;;
  esac
fi
export CHROOT_NAME
export TARGET
