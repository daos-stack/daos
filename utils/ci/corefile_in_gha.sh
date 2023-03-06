#!/bin/bash

cd daos || exit 1

echo ::group::Check for corefiles

echo BASE_DISTRO="${BASE_DISTRO}"
echo -n core_pattern= ; cat /proc/sys/kernel/core_pattern

if [ $(echo "$BASE_DISTRO" | grep -E -q -i 'ubuntu|alma|rocky') ]
then
    # on GHA internal runners running 'ubuntu|alma|rocky', "apport" seems to be
    # used ...
    # COREFILE_DIR="/var/crash/"
    # or trying to set corefile repo in current dir
    # COREFILE_DIR=$(pwd)
    COREFILE_DIR=/tmp/
else
    COREFILE_DIR="/var/lib/systemd/coredump/"
fi

if [ $(find "$COREFILE_DIR" -maxdepth 1 -type f | grep -c core) == 0 ]
then
    echo "no corefile in $COREFILE_DIR"
    exit 0
fi
for i in $(find "$COREFILE_DIR"/core* -print 2>/dev/null) ; do
    ls -ltr "$i"
    file "$i"
    gdb /opt/daos/bin/daos_engine "$i" <<eof
set pagination off
bt full
disass ABT_thread_create
quit
eof
done

#for i in $(sudo find / -type f -name core.*) ; do
#    ls -ltr "$i"
#    file "$i"
#done

echo ::endgroup::

