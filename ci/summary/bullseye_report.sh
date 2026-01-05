#!/bin/bash

# Script for installing packages used for CI summary steps
set -uex

#!/bin/bash

# Script for generating a bullseye code coverage report
set -eux

if [ ! -d '/opt/BullseyeCoverage/bin' ]; then
  echo 'Bullseye not found.'
  exit 1
fi
export COVFILE="$WORKSPACE/test.cov"
export PATH="/opt/BullseyeCoverage/bin:$PATH"

# Merge all coverage files
cp /opt/BullseyeCoverage/daos/test.cov "${COVFILE}"
readarray -t cov_files < <(find "${WORKSPACE}" -name test.cov)
if [ ${#cov_files[@]} -gt 0 ]; then
  covmerge --no-banner --file "${COVFILE}" "${cov_files[@]}"
fi

if [ ! -e "$COVFILE" ]; then
  echo "Coverage file ${COVFILE} is missing"
  exit 1
else
  ls -al "${COVFILE}"
fi

# Exclude tests
covselect --file test.cov --source "*/src/tests/*" --disable
covselect --file test.cov --source "*/src/bio/smd/tests/*" --disable
covselect --file test.cov --source "*/src/cart/crt_self_test.h" --disable
covselect --file test.cov --source "*/src/cart/crt_self_test_client.c" --disable
covselect --file test.cov --source "*/src/cart/crt_self_test_service.c" --disable
covselect --file test.cov --source "*/src/client/api/tests/*" --disable
covselect --file test.cov --source "*/src/common/tests/*" --disable
covselect --file test.cov --source "*/src/common/tests_dmg_helpers.c" --disable
covselect --file test.cov --source "*/src/common/tests_lib.c" --disable
covselect --file test.cov --source "*/src/dtx/tests/*" --disable
covselect --file test.cov --source "*/src/engine/tests/*" --disable
covselect --file test.cov --source "*/src/gurt/examples/*" --disable
covselect --file test.cov --source "*/src/gurt/tests/*" --disable
covselect --file test.cov --source "*/src/mgmt/tests/*" --disable
covselect --file test.cov --source "*/src/object/tests/*" --disable
covselect --file test.cov --source "*/src/placement/tests/*" --disable
covselect --file test.cov --source "*/src/rdb/tests/*" --disable
covselect --file test.cov --source "*/src/security/tests/*" --disable
# covselect --file test.cov --source "*/src/utils/daos_autotest.c" --disable
covselect --file test.cov --source "*/src/utils/crt_launch/*" --disable
covselect --file test.cov --source "*/src/utils/self_test/*" --disable
covselect --file test.cov --source "*/src/vea/tests/*" --disable
covselect --file test.cov --source "*/src/vos/tests/*" --disable
ls -al "${COVFILE}"

# Generate the report
java -jar bullshtml.jar bullseye_code_coverage_report
ls -al bullseye_code_coverage_report
