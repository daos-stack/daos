#!/bin/bash

set -e
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

# Load needed variables
source ./configure.sh

log() {
  local msg="|  $1  |"
  line=$(printf "${msg}" | sed 's/./-/g')
  tput setaf 14 # set Cyan color
  printf -- "\n${line}\n${msg}\n${line}\n"
  tput sgr0 # reset color
}

for server in ${SERVERS}
do
    log "Cleaning ${server}"
    ssh ${server} "rm -f .ssh/known_hosts"
    ssh ${server} "sudo systemctl stop daos_server"
    ssh ${server} "sudo rm -rf /var/daos/ram/*"
    ssh ${server} "sudo umount /var/daos/ram/ && echo success || echo unmounted"

    # Set nr_hugepages value
    # nr_hugepages = (targets * 1Gib) / hugepagesize
    #    Example: for 8 targets and Hugepagesize = 2048 kB:
    #       Targets = 8
    #       1Gib = 1048576 KiB
    #       Hugepagesize = 2048kB
    #       nr_hugepages=(8*1048576) / 2048
    #       So nr_hugepages value is 4096
    hugepagesize=$(ssh ${server} "grep Hugepagesize /proc/meminfo | awk '{print \$2}'")
    nr_hugepages=$(( (${DAOS_SERVER_DISK_COUNT}*1048576) / ${hugepagesize} ))
    ssh ${server} "sudo sed -i \"s/^nr_hugepages:.*/nr_hugepages: ${nr_hugepages}/g\" /etc/daos/daos_server.yml"

    ssh ${server} "sudo sed -i \"s/^crt_timeout:.*/crt_timeout: ${DAOS_SERVER_CRT_TIMEOUT}/g\" /etc/daos/daos_server.yml"

    # storage settings
    ssh ${server} "sudo sed -i \"s/^\(\s*\)targets:.*/\1targets: ${DAOS_SERVER_DISK_COUNT}/g\" /etc/daos/daos_server.yml"
    ssh ${server} "sudo sed -i \"s/^\(\s*\)scm_size:.*/\1scm_size: ${DAOS_SERVER_SCM_SIZE}/g\" /etc/daos/daos_server.yml"

    ssh ${server} "cat /etc/daos/daos_server.yml"
    ssh ${server} "sudo systemctl start daos_server"
    sleep 4
    ssh ${server} "sudo systemctl status daos_server"
    printf "\nFinished cleaning ${server}\n\n"
done
