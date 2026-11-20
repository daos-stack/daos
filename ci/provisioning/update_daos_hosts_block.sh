#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eux

if [ $# -ne 1 ]; then
    echo "Usage: $0 <generated-hosts-file>"
    exit 2
fi

generated_hosts_file="$1"
base_file="/var/tmp/hosts.base"

if [ ! -s "$generated_hosts_file" ]; then
    echo "ERROR: Missing or empty hosts file: $generated_hosts_file"
    exit 1
fi

awk 'BEGIN{skip=0}
/^# BEGIN DAOS CI HOSTS$/{skip=1; next}
/^# END DAOS CI HOSTS$/{skip=0; next}
!skip{print}' /etc/hosts > "$base_file"

{
    echo '# BEGIN DAOS CI HOSTS'
    cat "$generated_hosts_file"
    echo '# END DAOS CI HOSTS'
} >> "$base_file"
cp "$base_file" /etc/hosts
