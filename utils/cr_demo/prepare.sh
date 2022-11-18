#!/bin/bash

if [ $# == 0 ]
then
    echo "Please supply hostname."
    exit
fi

hosts=$1

clush -w "$hosts" "sudo systemctl stop daos_server;\
                 sudo umount /mnt/daos;\
                 sudo systemctl start daos_server;"
