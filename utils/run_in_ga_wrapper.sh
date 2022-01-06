#!/bin/sh

set -e

docker run --name build-post --mount type=tmpfs,destination=/mnt/daos_0,tmpfs-mode=1777 \
       --env COMPILER --env DEPS_JOBS --user root:root build-image ./daos/utils/run_in_ga.sh

docker cp build-post:/home/daos/daos/nlt-junit.xml ./
