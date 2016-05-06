#!/bin/sh

set -e
set -x

# Build script used by Jenkins, this file should not be run by
# developers.

DAOS_INSTALL=${CORAL_ARTIFACTS}/${JOB_NAME}/${BUILD_NUMBER}
# Links are resolved by prereq_tools and target is saved
MERCURY=${CORAL_ARTIFACTS}/mercury-update-scratch/latest
OMPI=${CORAL_ARTIFACTS}/ompi-update-scratch/latest
NVML=${CORAL_ARTIFACTS}/nvml-update-scratch/latest
MCL=$(readlink -f ${CORAL_ARTIFACTS}/mcl-update-scratch/latest)
if [ "${JOB_NAME}" != "daos-update-scratch" ]; then
  # Review jobs should pull from latest stable scratch job
  DAOS=$(readlink -f ${CORAL_ARTIFACTS}/daos-update-scratch/latest)
  VARS=${DAOS}/.build_vars.sh
  if [ -f ${VARS} ]; then
    source ${VARS}
    OMPI=${SL_OMPI_PREFIX}/..
    MERCURY=${SL_MERCURY_PREFIX}/..
    NVML=${SL_NVML_PREFIX}/..
    MCL=${SL_MCL_PREFIX}/..
  fi
else
  # Use latest stable mcl job to get stable ompi and mercury
  VARS=${MCL}/.build_vars.sh
  if [ -f ${VARS} ]; then
    source ${VARS}
    OMPI=${SL_OMPI_PREFIX}/..
    MERCURY=${SL_MERCURY_PREFIX}/..
  fi
fi

rm -f *.conf
scons PREBUILT_PREFIX=${MERCURY}:${OMPI}:${NVML}:${MCL} -c \
      PREFIX=${DAOS_INSTALL}
scons
scons install

ln -sfn ${DAOS_INSTALL} ${CORAL_ARTIFACTS}/${JOB_NAME}/latest
cp .build_vars.sh ${DAOS_INSTALL}
cp .build_vars.py ${DAOS_INSTALL}
