#!/bin/sh

set -e

IMAGE=$(docker images --all --filter label=DAOS=true --quiet | tail -n 1)

docker run --name build-post --cap-add SYS_ADMIN --cap-add SYS_PTRACE \
       --env COMPILER "$IMAGE" ./utils/run_in_ga.sh

docker cp build-post:/home/daos/daos/nlt-junit.xml ./
