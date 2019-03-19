#!/bin/sh

LOCAL_PATH=$1
SHUTDOWN=${LOCAL_PATH}/.ctrl/shutdown

max=10
while [ $max -gt 0 ]; do
  stat ${SHUTDOWN}
  retcode=$?
  if [ $retcode -eq 0 ]; then
    break
  fi
  max=$[ $max - 1 ]
  sleep 2
done

if [ $retcode -ne 0 ]; then
  exit 1
fi

touch ${SHUTDOWN}
