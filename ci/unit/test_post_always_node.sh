#!/bin/bash

set -uex

cd "$DAOS_BASE"
test_log_dir="run_test.sh"
case $STAGE_NAME in
  *Bullseye*)
    test_log_dir="covc_test_logs"
    ;;
  *memcheck*)
    test_log_dir="unit_test_memcheck_logs"
    ;;
  *Unit*)
    test_log_dir="unit_test_logs"
    ;;
esac
mkdir "${test_log_dir}"
if ls /tmp/daos*.log > /dev/null; then
  mv /tmp/daos*.log "$test_log_dir"/
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
