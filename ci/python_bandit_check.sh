#!/bin/bash

set -uex

git clean -dxf

bandit --format xml -o bandit.xml -r . \
       --exclude ./utils/rpms/_topdir,./deps \
       -c ci/bandit.config || true
