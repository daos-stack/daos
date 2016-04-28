#!/bin/sh

set -e
set -x

# Build script used by Jenkins, this file should not be run by
# developers.

# Links are resolved by prereq_tools and target is saved
MERCURY=${CORAL_ARTIFACTS}/mercury-update-scratch/latest
OMPI=${CORAL_ARTIFACTS}/ompi-update-scratch/latest
NVML=${CORAL_ARTIFACTS}/nvml-update-scratch/latest
MCL=${CORAL_ARTIFACTS}/mcl-update-scratch/latest

rm -f *.conf
scons PREBUILT_PREFIX=${MERCURY}:${OMPI}:${NVML}:${MCL} -c
scons
scons install
