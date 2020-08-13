#!/bin/bash

set -uex

cd "$DAOS_BASE"
mkdir run_test.sh
mkdir vm_test
mv nlt-errors.json vm_test/
if ls /tmp/daos*.log > /dev/null; then
  mv /tmp/daos*.log run_test.sh/
fi
if ls /tmp/dnt*.log > /dev/null; then
  mv /tmp/dnt*.log vm_test/
fi
# servers can sometimes take a while to stop when the test is done
x=0
while [ "$x" -lt 10 ] &&
  pgrep '(orterun|daos_server|daos_io_server)'; do
  sleep 1
  (( x=x+1 ))
done
if ! sudo umount /mnt/daos; then
  echo 'Failed to unmount /mnt/daos'
  ps axf
fi
cd
if ! sudo umount "$DAOS_BASE"; then
  echo 'Failed to unmount '"$DAOS_BASE"
  ps axf
fi
