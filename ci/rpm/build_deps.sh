#!/bin/bash
code_coverage="${1:-false}"
bullseye_key="${2:-}"

cd /home/daos/pre || exit 1
scons install --build-deps=only USE_INSTALLED=all PREFIX=/opt/daos TARGET_TYPE=release -j 32

if [ "${code_coverage}" == "true" ] ; then
    mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
    pushd "${mydir}/../.." || exit 1
    utils/rpms/prep_other_deps.sh "${bullseye_key}"
    ls -aR
fi
