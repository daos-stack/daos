#!/bin/sh
# A Script that for now simply kicks off vos unit tests. Can later
#  be expanded to run other unit tests as this script is
#  executed in the Jenkins environment.
#  the flock is to ensure that only one jenkins builder
#  runs the tests at a time, as the tests use the same
#  mount point.  For now, there actually isn't much
#  contention, however, because /mnt/daos on the jenkins nodes
#  is subdivided into /mnt/daos/el7; /mnt/daos/sles12sp3;
#  /mnt/daos/ubuntu1404; and /mnt/daos/ubuntu1604
#  These are mapped into /mnt/daos in the docker containers
#  for the corresponding Jenkins distro builder. This "magic"
#  is configured in the Jenkins config page for daos builders
#  in the "build" section.
#
#  Note: $DIST_TARGET/daos is where all daos artifacts are
#    located in the docker container when a build completes
#  Note: if further tests are added, be sure to modify
#    scons_local/utils/docker/daos_post_build.sh
#    to scan for the updated success or failure text

#check for existence of /mnt/daos first:
if [ -d "/mnt/daos" ]; then
    time flock /mnt/daos/jenkins.lock ${DIST_TARGET}/daos/bin/vos_tests -A 500
else
    echo "/mnt/daos isn't present for unit tests"
fi
