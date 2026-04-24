#!/bin/bash

# This is a file to be sourced by other files needing to parse
# the jenkins environment variables for a build stage to determine
# what specifically they need to build.
set -uex

if [ -n "${STAGE_NAME:?}" ]; then
  case $STAGE_NAME in
    *CentOS\ 7*|*el7*|*centos7*)
      : "${TARGET:=centos7}"
      : "${REPO_SPEC:=el-7}"
      ;;
    *CentOS\ 8*|*EL\ 8*|*el8*|*centos8*)
      : "${TARGET:=centos8}"
      : "${REPO_SPEC:=el-8}"
      ;;
    *CentOS\ 9*|*EL\ 9*|*el9*|*centos9*)
      : "${TARGET:=centos9}"
      : "${REPO_SPEC:=el-9}"
      ;;
    *Leap\ 15.6*|*leap15.6*|*opensuse15.6*|*sles15.6*)
      : "${TARGET:=leap15.6}"
      ;;
    *Leap\ 15.5*|*leap15.5*|*opensuse15.5*|*sles15.5*)
      : "${TARGET:=leap15.5}"
      ;;
    *Leap\ 15.4*|*leap15.4*|*opensuse15.4*|*sles15.4*)
      : "${TARGET:=leap15.4}"
      ;;
    *Leap\ 15.3*|*leap15.3*|*opensuse15.3*|*sles15.3*)
      : "${TARGET:=leap15.3}"
      ;;
    *Leap\ 15*|*leap15*|*opensuse15*|*sles15*)
      : "${TARGET:=leap15}"
      : "${REPO_SPEC:=sl-15}"
      ;;
    *Ubuntu\ 20.04*|*ubuntu2004*)
      : "${TARGET:=ubuntu20}"
      : "${REPO_SPEC:=ubuntu-20.04}"
      ;;
    *Ubuntu\ 22.04*|*ubuntu2204*)
      : "${TARGET:=ubuntu22}"
      : "${REPO_SPEC:=ubuntu-22.04}"
      ;;
  esac
fi
export TARGET
