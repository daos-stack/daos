#!/bin/bash

set -e
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

# Load needed variables
source ./configure.sh

for server in ${SERVERS}
do
    echo "#######################"
    echo "#  Cleaning ${server}"
    echo "#######################"
    ssh ${server} "rm -f .ssh/known_hosts"
    ssh ${server} "sudo systemctl stop daos_server"
    ssh ${server} "sudo rm -rf /var/daos/ram/*"
    ssh ${server} "sudo umount /var/daos/ram/ && echo success || echo unmounted"
    ssh ${server} "sudo sed -i \"s/^crt_timeout:.*/crt_timeout: ${CRT_TIMEOUT}/g\" /etc/daos/daos_server.yml"
    ssh ${server} "sudo sed -i \"s/^   targets:.*/   targets: ${DAOS_DISK_COUNT}/g\" /etc/daos/daos_server.yml"
    ssh ${server} "sudo sed -i \"s/^   scm_size:.*/   scm_size: ${SCM_SIZE}/g\" /etc/daos/daos_server.yml"
    ssh ${server} "cat /etc/daos/daos_server.yml"
    ssh ${server} "sudo systemctl start daos_server"
    sleep 4
    ssh ${server} "sudo systemctl status daos_server"
    echo "Done"
done
