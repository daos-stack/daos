#!/bin/bash

set -eux

wait_nodes() {
    for host in ${NODESTRING//,/ }; do
        START=$SECONDS
        while [ $((SECONDS-START)) -lt 420 ]; do
            if echo >/dev/tcp/"$host"/22; then
                exit 0
            fi
            sleep 1
            continue
        done
    done

    exit 1
}

#shellcheck disable=SC2153
if ! POOL="${POOL:-}" restore_vm_snapshot.sh daos_ci-"$DISTRO" "$NODESTRING"; then
    rc=${PIPESTATUS[0]}
    distro="$DISTRO"
    while [[ $distro = *.* ]]; do
        distro=${distro%.*}
        if ! POOL="" restore_vm_snapshot.sh daos_ci-"${distro}" "$NODESTRING"; then
            rc=${PIPESTATUS[0]}
            continue
        fi
        wait_nodes
    done
    exit "$rc"
fi

wait_nodes