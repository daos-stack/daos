#!/bin/bash

# sudo required
mount -o dax /dev/pmem0 /mnt/pmem0/
chown jmichal:jmichal -R /mnt/pmem0/
tree /mnt/pmem0/
