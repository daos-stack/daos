#!/bin/bash

cd daos || exit 1

echo ::group::Check for corefiles

echo BASE_DISTRO=${BASE_DISTRO}
echo -n core_pattern= ; cat /proc/sys/kernel/core_pattern

if [ $(echo "$BASE_DISTRO" | egrep -i 'ubuntu|alma|rocky') ]
then
    # on GHA internal runners running 'ubuntu|alma|rocky', "apport" seems to be
    # used ...
    # COREFILE_DIR="/var/crash/"
    # trying to set corefile repo in current dir
    COREFILE_DIR="./"
else
    COREFILE_DIR="/var/lib/systemd/coredump/"
fi

if [ $(find "$COREFILE_DIR" -maxdepth 1 -type f | grep core | wc -l) == 0 ]
then
    echo "no corefile in $COREFILE_DIR"
    exit 0
fi
for i in $COREFILE_DIR/core* ; do
    ls -ltr "$i"
    file "$i"
done
echo ::endgroup::

