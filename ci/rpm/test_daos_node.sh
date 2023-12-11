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
        export MODULEPATH=/usr/share/modules
        ;;
esac

if [ -n "$DAOS_PKG_VERSION" ]; then
    DAOS_PKG_VERSION="-${DAOS_PKG_VERSION}"
fi

set -uex
sudo $YUM -y install daos-client"$DAOS_PKG_VERSION"
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
sudo $YUM -y install daos-client-tests"$DAOS_PKG_VERSION"
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
sudo $YUM -y install daos-server-tests"$DAOS_PKG_VERSION"
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
sudo $YUM -y install --exclude ompi daos-client-tests-openmpi"$DAOS_PKG_VERSION"
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
sudo $YUM -y install daos-server"$DAOS_PKG_VERSION"
if rpm -q daos-client; then
  echo "daos-client RPM should not be installed as a dependency of daos-server"
  exit 1
fi

sudo $YUM -y install --exclude ompi daos-client-tests-openmpi"$DAOS_PKG_VERSION"

me=$(whoami)
for dir in server agent; do
  sudo mkdir "/var/run/daos_$dir"
  sudo chmod 0755 "/var/run/daos_$dir"
  sudo chown "$me:$me" "/var/run/daos_$dir"
done
sudo mkdir /tmp/daos_sockets
sudo chmod 0755 /tmp/daos_sockets
sudo chown "$me:$me" /tmp/daos_sockets

FTEST=/usr/lib/daos/TESTING/ftest
sudo PYTHONPATH="$FTEST/util"                               \
     $FTEST/config_file_gen.py -n "$HOSTNAME"               \
                               -a /etc/daos/daos_agent.yml  \
                               -s /etc/daos/daos_server.yml
sudo bash -c 'echo "system_ram_reserved: 4" >> /etc/daos/daos_server.yml'
sudo PYTHONPATH="$FTEST/util"                        \
     $FTEST/config_file_gen.py -n "$HOSTNAME"        \
                               -d /etc/daos/daos_control.yml
cat /etc/daos/daos_server.yml
cat /etc/daos/daos_agent.yml
cat /etc/daos/daos_control.yml
if ! module load "$OPENMPI"; then
    echo "Unable to load OpenMPI module: $OPENMPI"
    module avail
    module list
    exit 1
fi

export POOL_NVME_SIZE=0

coproc SERVER { exec daos_server --debug start -t 1; } 2>&1
trap 'set -x; kill -INT $SERVER_PID' EXIT
line=""
stdout=()
deadline=$((SECONDS+300))
while [[ $line != *started\ on\ rank\ 0* ]] && [ "$SECONDS" -lt "$deadline" ]; do
    if ! read -r -t 60 line <&"${SERVER[0]}"; then
        rc=${PIPESTATUS[0]}
        if [ "$rc" = "142" ]; then
            echo "Timed out waiting for output from the server"
        else
            echo "Error reading the output from the server: $rc"
        fi
        echo "Server output:"
        export IFS=$'\n'
        echo "${stdout[*]}"
        exit "$rc"
    fi
    echo "Server stdout: $line"
    stdout+=("$line")
    if [[ $line == SCM\ format\ required\ on\ instance\ * ]]; then
        dmg storage format -l "$HOSTNAME" --force
        if ! dmg system query -v; then
            sleep 5
            dmg system query -v || true
        fi
    fi
done
if [ "$SECONDS" -ge "$deadline" ]; then
    echo "Timed out waiting for server to start"
    echo "Server output:"
    export IFS=$'\n'
    echo "${stdout[*]}"
    exit 1
fi
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
