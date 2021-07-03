#!/bin/bash

set -eux

if ls -lh /tmp/*.log 2>/dev/null; then
    rm -f /tmp/server*.log /tmp/engine*.log /tmp/daos*.log
fi
if ls -lh /localhome/jenkins/.spdk* 2>/dev/null; then
    rm -f /localhome/jenkins/.spdk*
fi
if [ -d /var/tmp/daos_testing/ ]; then
    ls -lh /var/tmp/daos_testing/
    rm -rf /var/tmp/daos_testing/
fi
if ls /tmp/Functional_*/; then
    rm -rf /tmp/Functional_*
fi
