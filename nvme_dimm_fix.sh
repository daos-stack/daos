#!/bin/bash

set -uex

tnode=${NODELIST##*,}

idstr='Non-Volatile memory controller: '
idstr+='Intel Corporation NVMe Datacenter SSD.*Optane'
# shellcheck disable=SC2029
nvme_ids=($(ssh "${SSH_KEY_ARGS}" "root@${tnode}" \
  "lspci | grep -i \"${idstr}\"" | awk '{print $1}' | head -n 2))

for f in install/lib/daos/TESTING/ftest/*/*.yaml; do
  sed -i "s/0000:81:00.0/0000:${nvme_ids[0]}/g" "${f}"
  sed -i "s/0000:da:00.0/0000:${nvme_ids[1]}/g" "${f}"
done
