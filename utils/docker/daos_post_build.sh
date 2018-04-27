#!/bin/sh
test_results="docker_build.log"

ls ${test_results}

if [ "$DAOS_RUN_UNIT_TESTS" -eq "1" ]
then
        grep "SUCCESS! NO TEST FAILURES" ${test_results}
else
        grep "Daos unit tests skipped." ${test_results}
fi
