#!/bin/bash

set -uex

cd "$DAOS_BASE"

if [ "$WITH_VALGRIND" = "memcheck" ]; then
    mkdir run_test_memcheck.sh
    if ls /tmp/daos*.log > /dev/null; then
      mv /tmp/daos*.log run_test_memcheck.sh/
    fi
    if ls test_results/*.xml > /dev/null; then
      mv test_results/*.xml run_test_memcheck.sh/
    fi
elif [ "$WITH_VALGRIND" = "disabled" ]; then
    mkdir run_test.sh
    mkdir vm_test
    mv nlt-errors.json vm_test/
    if ls /tmp/daos*.log > /dev/null; then
      mv /tmp/daos*.log run_test.sh/
    fi
    if ls /tmp/dnt*.log > /dev/null; then
      mv /tmp/dnt*.log vm_test/
    fi
fi

# servers can sometimes take a while to stop when the test is done
x=0
while [ "$x" -lt 10 ] &&
  pgrep '(orterun|daos_server|daos_io_server)'; do
  sleep 1
  let x=$x+1
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
