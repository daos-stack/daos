#!/bin/bash

YUM=dnf
if [ "$(lsb_release -si)" = "CentOS" ]; then
    if [[ $(lsb_release -sr) = 8* ]]; then
        OPENMPI=mpi/openmpi-x86_64
    else
        OPENMPI=mpi/openmpi3-x86_64
    fi
elif [ "$(lsb_release -si)" = "openSUSE" ]; then
    OPENMPI=gnu-openmpi
fi

set -uex
sudo $YUM -y install --exclude ompi,libpmemobj,argobots,spdk \
     daos-client-"${DAOS_PKG_VERSION}"
if rpm -q daos-server; then
  echo "daos-server RPM should not be installed as a dependency of daos-client"
  exit 1
fi
sudo $YUM -y history rollback last-1
sudo $YUM -y install --exclude ompi daos-server-"${DAOS_PKG_VERSION}"
if rpm -q daos-client; then
  echo "daos-client RPM should not be installed as a dependency of daos-server"
  exit 1
fi
sudo $YUM -y install --exclude ompi daos-tests-"${DAOS_PKG_VERSION}"

me=$(whoami)
for dir in server agent; do
  sudo mkdir "/var/run/daos_$dir"
  sudo chmod 0755 "/var/run/daos_$dir"
  sudo chown "$me:$me" "/var/run/daos_$dir"
done
sudo mkdir /tmp/daos_sockets
sudo chmod 0755 /tmp/daos_sockets
sudo chown "$me:$me" /tmp/daos_sockets
sudo mkdir -p /mnt/daos
sudo mount -t tmpfs -o size=16777216k tmpfs /mnt/daos
sudo cp /tmp/daos_server.yml /etc/daos/daos_server.yml
sudo cp /tmp/daos_agent.yml /etc/daos/daos_agent.yml
sudo cp /tmp/dmg.yml /etc/daos/daos.yml
cat /etc/daos/daos_server.yml
cat /etc/daos/daos_agent.yml
cat /etc/daos/daos.yml
if ! module load $OPENMPI; then
    echo "Unable to load OpenMPI module: $OPENMPI"
    module avail
    module list
    exit 1
fi
coproc daos_server --debug start -t 1 --recreate-superblocks
trap 'set -x; kill -INT $COPROC_PID' EXIT
line=""
while [[ "$line" != *started\ on\ rank\ 0* ]]; do
  read -r -t 60 line <&"${COPROC[0]}"
  echo "Server stdout: $line"
done
echo "Server started!"
daos_agent --debug &
AGENT_PID=$!
trap 'set -x; kill -INT $AGENT_PID $COPROC_PID' EXIT
OFI_INTERFACE=eth0 daos_test -m
