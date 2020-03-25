#!/bin/sh
test_results="docker_build.log"

ls ${test_results}

: "${DAOS_RUN_UNIT_TESTS:=0}"

if [ "$DAOS_RUN_UNIT_TESTS" -eq "1" ]
then
        grep "SUCCESS! NO TEST FAILURES" ${test_results}
else
        echo "Daos unit tests skipped."
fi
