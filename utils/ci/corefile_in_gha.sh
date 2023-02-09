#!/bin/bash

cd daos || exit 1

echo ::group::Check for corefiles

echo BASE_DISTRO=${BASE_DISTRO}
echo -n core_pattern ; cat /proc/sys/kernel/core_pattern

if [ "$BASE_DISTRO" == "ubuntu" ]
then
    # "apport" corefile repo
    COREFILE_DIR="/var/crash/"
else
    COREFILE_DIR="/var/lib/systemd/coredump/"
fi
if [ $(find "$COREFILE_DIR" -maxdepth 1 -type f | wc -l) == 0 ]
then
    echo "no corefile in $COREFILE_DIR."
    exit 0
fi
for i in "$COREFILE_DIR"/* ; do
    ls -l "$i"
    file "$i"
done
echo ::endgroup::

