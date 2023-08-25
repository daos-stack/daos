#!/bin/bash

. /etc/os-release

YUM=dnf
case "$ID_LIKE" in
    *rhel*)
        if [[ $VERSION_ID = [89].* ]]; then
            OPENMPI_RPM=openmpi
            OPENMPI=mpi/openmpi-x86_64
        else
            OPENMPI_RPM=openmpi3
            OPENMPI=mpi/openmpi3-x86_64
        fi
        ;;
    *suse*)
        OPENMPI_RPM=openmpi3
        OPENMPI=gnu-openmpi
        ;;
esac

set -uex
sudo $YUM -y install daos-client-"${DAOS_PKG_VERSION}"
if rpm -q daos-server; then
  echo "daos-server RPM should not be installed as a dependency of daos-client"
  exit 1
fi
if ! sudo $YUM -y history undo last; then
    echo "Error trying to undo previous dnf transaction"
    $YUM history
    exit 1
fi
sudo $YUM -y erase "$OPENMPI_RPM"
sudo $YUM -y install daos-client-tests-"${DAOS_PKG_VERSION}"
if rpm -q "$OPENMPI_RPM"; then
  echo "$OPENMPI_RPM RPM should not be installed as a dependency of daos-client-tests"
  exit 1
fi
if ! rpm -q daos-admin; then
    echo "daos-admin should be installed as a dependency of daos-client-tests"
    exit 1
fi
if ! sudo $YUM -y history undo last; then
    echo "Error trying to undo previous dnf transaction"
    $YUM history
    exit 1
fi
sudo $YUM -y install daos-server-tests-"${DAOS_PKG_VERSION}"
if rpm -q "$OPENMPI_RPM"; then
  echo "$OPENMPI_RPM RPM should not be installed as a dependency of daos-server-tests"
  exit 1
fi
if ! rpm -q daos-admin; then
    echo "daos-admin should be installed as a dependency of daos-server-tests"
    exit 1
fi
if ! sudo $YUM -y history undo last; then
    echo "Error trying to undo previous dnf transaction"
    $YUM history
    exit 1
fi
sudo $YUM -y install --exclude ompi daos-client-tests-openmpi-"${DAOS_PKG_VERSION}"
if ! rpm -q daos-client; then
  echo "daos-client RPM should be installed as a dependency of daos-client-tests-openmpi"
  exit 1
fi
if rpm -q daos-server; then
  echo "daos-server RPM should not be installed as a dependency of daos-client-tests-openmpi"
  exit 1
fi
if ! rpm -q daos-client-tests; then
  echo "daos-client-tests RPM should be installed as a dependency of daos-client-tests-openmpi"
  exit 1
fi
if ! sudo $YUM -y history undo last; then
    echo "Error trying to undo previous dnf transaction"
    $YUM history
    exit 1
fi
sudo $YUM -y install daos-server-"${DAOS_PKG_VERSION}"
if rpm -q daos-client; then
  echo "daos-client RPM should not be installed as a dependency of daos-server"
  exit 1
fi

sudo $YUM -y install --exclude ompi daos-client-tests-openmpi-"${DAOS_PKG_VERSION}"

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

FTEST=/usr/lib/daos/TESTING/ftest
sudo PYTHONPATH="$FTEST/util"                               \
     $FTEST/config_file_gen.py -n "$HOSTNAME"               \
                               -a /etc/daos/daos_agent.yml  \
                               -s /etc/daos/daos_server.yml
sudo PYTHONPATH="$FTEST/util"                        \
     $FTEST/config_file_gen.py -n "$HOSTNAME"        \
                               -d /etc/daos/daos.yml
cat /etc/daos/daos_server.yml
cat /etc/daos/daos_agent.yml
cat /etc/daos/daos.yml
if ! module load "$OPENMPI"; then
    echo "Unable to load OpenMPI module: $OPENMPI"
    module avail
    module list
    exit 1
fi

coproc SERVER { exec daos_server --debug start -t 1 --recreate-superblocks; } 2>&1
trap 'set -x; kill -INT $SERVER_PID' EXIT
line=""
while [[ "$line" != *started\ on\ rank\ 0* ]]; do
  if ! read -r -t 60 line <&"${SERVER[0]}"; then
      rc=${PIPESTATUS[0]}
      if [ "$rc" = "142" ]; then
          echo "Timed out waiting for output from the server"
      else
          echo "Error reading the output from the server: $rc"
      fi
      exit "$rc"
  fi
  echo "Server stdout: $line"
done
echo "Server started!"
coproc AGENT { exec daos_agent --debug; } 2>&1
trap 'set -x; kill -INT $AGENT_PID $SERVER_PID' EXIT
line=""
while [[ "$line" != *listening\ on\ * ]]; do
  if ! read -r -t 60 line <&"${AGENT[0]}"; then
      rc=${PIPESTATUS[0]}
      if [ "$rc" = "142" ]; then
          echo "Timed out waiting for output from the agent"
      else
          echo "Error reading the output from the agent: $rc"
      fi
      exit "$rc"
  fi
  echo "Agent stdout: $line"
done
echo "Agent started!"
if ! OFI_INTERFACE=eth0 timeout -k 30 300 daos_test -m; then
    rc=${PIPESTATUS[0]}
    if [ "$rc" = "124" ]; then
        echo "daos_test -m was killed after running for 5 minutes"
    else
        echo "daos_test -m failed, exiting with $rc"
    fi
    echo "daos_server stdout and stderr since rank 0 started:"
    timeout -k 30 120 cat <&"${SERVER[0]}"
    exit "$rc"
fi
exit 0
