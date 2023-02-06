#!/bin/bash

cd daos

echo ::group::Check for corefiles
if [ $BASE_DISTRO == "ubuntu" ]
then
    # "apport" corefile repo
    COREFILE_DIR="/var/crash/"
else
    COREFILE_DIR="/var/lib/systemd/coredump/"
fi
for i in `ls -1 $COREFILE_DIR/` ; do
    ls -l $i
    file $i
done
echo ::endgroup::

