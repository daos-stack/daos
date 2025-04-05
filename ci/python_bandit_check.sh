#!/bin/bash

set -uex

git clean -dxf
# Nothing seems to work to filter the submodules but as they are separate
# projects, things like bandit should be in those projects, not in DAOS.
rm -rf deps/{spdk,mercury,ofi,argobots,fused,protobufc,isal,isal_crypto,pmdk,ucx}

bandit --format xml -o bandit.xml -r . \
       --exclude ./utils/rpms/_topdir \
       -c ci/bandit.config || true
