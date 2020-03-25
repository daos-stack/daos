#!/bin/bash

test_results="docker_build.log"

ls "${test_results}"

grep "Sanity test PASSED" "${test_results}"

