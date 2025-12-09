#!/bin/bash -e

nvme  delete-ns /dev/nvme0 -n 0x1
nvme reset /dev/nvme0
nvme create-ns /dev/nvme0 --nsze=0x1bf1f72b0 --ncap=0x1bf1f72b0 --flbas=0
nvme attach-ns /dev/nvme0 -n 0x1 -c 0x41
nvme reset /dev/nvme0

nvme  delete-ns /dev/nvme1 -n 0x1
nvme reset /dev/nvme1
nvme create-ns /dev/nvme1 --nsze=0x1bf1f72b0 --ncap=0x1bf1f72b0 --flbas=0
nvme attach-ns /dev/nvme1 -n 0x1 -c 0x41
nvme reset /dev/nvme1

daos_server nvme scan