#!/bin/bash

set -eux

wait_nodes() {
    for host in ${NODESTRING//,/ }; do
        x=0
        while ! echo >/dev/tcp/"$host"/22 && [ $x -lt 420 ]; do
            sleep 1
            (( x++ )) || true
            continue
        done
    done

    if [ "$x" -ge 420 ]; then
        exit 1
    fi

    exit 0
}

#shellcheck disable=SC2153
if ! POOL="${POOL:-}" restore_vm_snapshot.sh daos_ci-"$DISTRO" "$NODESTRING"; then
    rc=${PIPESTATUS[0]}
    distro="$DISTRO"
    while [[ $distro = *.* ]]; do
        distro=${distro%.*}
        if ! POOL="" restore_vm_snapshot.sh daos_ci-${distro} "$NODESTRING"; then
            rc=${PIPESTATUS[0]}
            continue
        fi
        wait_nodes
    done
    exit "$rc"
fi

wait_nodes