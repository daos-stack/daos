#!/bin/bash

# set -e
set -x

docker stop daos-server-noauth daos-admin-noauth daios-client-noauth
docker rm daos-server-noauth daos-admin-noauth daios-client-noauth
docker run --privileged --cap-add=ALL --name=daos-server-noauth --hostname=daos-server --add-host "daos-server:10.8.1.67" --add-host "daos-admin:10.8.1.67" --add-host "daos-client:10.8.1.67" --detach --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro --volume=/dev/hugepages:/dev/hugepages --tmpfs=/run --network=host daos-server-noauth:centos8
docker run --privileged --cap-add=ALL --name=daos-admin-noauth --hostname=daos-admin  --add-host "daos-server:10.8.1.67" --add-host "daos-admin:10.8.1.67" --add-host "daos-client:10.8.1.67" --detach --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro --volume=/dev/hugepages:/dev/hugepages --tmpfs=/run --network=host daos-admin-noauth:centos8
docker run --privileged --cap-add=ALL --name=daos-client-noauth --hostname=daos-client  --add-host "daos-server:10.8.1.67" --add-host "daos-admin:10.8.1.67" --add-host "daos-client:10.8.1.67" --detach --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro --volume=/dev/hugepages:/dev/hugepages --tmpfs=/run --network=host daos-client-noauth:centos8
