#!/bin/sh

set -e

IMAGE=$(docker images --all --filter label=DAOS=true --quiet | tail -n 1)

docker run --name build-post \
       --mount type=tmpfs,destination=/mnt/daos_0,tmpfs-mode=1777 \
       --env COMPILER "$IMAGE" ./utils/run_in_ga.sh

docker cp build-post:/home/daos/daos/nlt-junit.xml ./
