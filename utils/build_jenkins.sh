#!/bin/sh

set -e
set -x

# Build script used by Jenkins, this file should not be run by
# developers.

# Links are resolved by prereq_tools and target is saved
MERCURY=${CORAL_ARTIFACTS}/mercury-update-scratch/latest
OMPI=${CORAL_ARTIFACTS}/ompi-update-scratch/latest
NVML=${CORAL_ARTIFACTS}/nvml-update-scratch/latest
IOF=${CORAL_ARTIFACTS}/iof-update-scratch/latest

rm -f *.conf
scons PREBUILT_PREFIX=${MERCURY}:${OMPI}:${NVML}:${IOF} -c
scons
scons install
