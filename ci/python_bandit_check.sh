#!/bin/bash

set -uex

bandit --format xml -o bandit.xml -r . \
       --exclude ./utils/rpms/_topdir \
       -c ci/bandit.config || true
