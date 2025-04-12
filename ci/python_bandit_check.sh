#!/bin/bash

set -uex

git clean -dxf
# Nothing seems to work to filter the submodules but as they are separate
# projects, things like bandit should be in those projects, not in DAOS.
rm -rf tmp
mkdir -p tmp
cp -R SConstruct debian src utils ci docs site_scons tmp

pushd tmp || exit 1
bandit --format xml -o bandit.xml -r . \
       --exclude ./utils/rpms/_topdir \
       -c ci/bandit.config || true
popd
