#!/usr/bin/env python3

#
# Copyright(c) 2012-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import tests_config
import os
import sys
import subprocess


def run_command(args):
    result = subprocess.run(" ".join(args), shell=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    result.stdout = result.stdout.decode("ASCII", errors='ignore')
    result.stderr = result.stderr.decode("ASCII", errors='ignore')
    return result


def rmv_cmd(trgt):
    """Remove target with force"""
    result = run_command(["rm", "-rf", trgt])
    if result.returncode != 0:
        raise Exception("Removing {} before testing failed!".
                        format(os.path.dirname(os.path.realpath(__file__))
                               + trgt))


def cleanup():
    """Delete files created by unit tests"""
    script_path = os.path.dirname(os.path.realpath(__file__))
    test_dir = os.path.join(script_path, tests_config.MAIN_DIRECTORY_OF_UNIT_TESTS)
    result = run_command(["cd", test_dir])
    if result.returncode != 0:
        raise Exception("Cleanup before testing failed!")

    # r=root, d=directories, f = files
    for r, d, f in os.walk(test_dir):
        for file in f:
            if '_generated_wrap' in file:
                rmv_cmd(file)

    rmv_cmd("preprocessed_sources_repository")
    rmv_cmd("sources_to_test_repository")
    rmv_cmd("build")

    result = run_command(["cd", script_path])
    if result.returncode != 0:
        raise Exception("Cleanup before testing failed!")


cleanup()

script_path = os.path.dirname(os.path.realpath(__file__))

main_UT_dir = os.path.join(script_path, tests_config.MAIN_DIRECTORY_OF_UNIT_TESTS)

main_env_dir = os.path.join(script_path, tests_config.MAIN_DIRECTORY_OF_ENV_FILES)

main_tested_dir = os.path.join(script_path, tests_config.MAIN_DIRECTORY_OF_TESTED_PROJECT)

if not os.path.isdir(os.path.join(main_UT_dir, "ocf_env", "ocf")):
    try:
        os.makedirs(os.path.join(main_UT_dir, "ocf_env", "ocf"))
    except Exception:
        raise Exception("Cannot create ocf_env/ocf directory!")

result = run_command(["ln", "-fs",
                      os.path.join(main_env_dir, "*"),
                      os.path.join(main_UT_dir, "ocf_env")])
if result.returncode != 0:
    raise Exception("Preparing env sources for testing failed!")

result = run_command(["cp", "-r",
                      os.path.join(main_tested_dir, "inc", "*"),
                      os.path.join(main_UT_dir, "ocf_env", "ocf")])
if result.returncode != 0:
    raise Exception("Preparing sources for testing failed!")

result = run_command([os.path.join(script_path, "prepare_sources_for_testing.py")])
if result.returncode != 0:
    raise Exception("Preparing sources for testing failed!")

build_dir = os.path.join(main_UT_dir, "build")
logs_dir = os.path.join(main_UT_dir, "logs")

try:
    if not os.path.isdir(build_dir):
        os.makedirs(build_dir)
    if not os.path.isdir(logs_dir):
        os.makedirs(logs_dir)
except Exception:
    raise Exception("Cannot create logs directory!")

os.chdir(build_dir)

cmake_result = run_command(["cmake", ".."])

print(cmake_result.stdout)
with open(os.path.join(logs_dir, "cmake.output"), "w") as f:
    f.write(cmake_result.stdout)
    f.write(cmake_result.stderr)

if cmake_result.returncode != 0:
    with open(os.path.join(logs_dir, "tests.output"), "w") as f:
        f.write("Cmake step failed! More details in cmake.output.")
    sys.exit(1)

make_result = run_command(["make", "-j"])

print(make_result.stdout)
with open(os.path.join(logs_dir, "make.output"), "w") as f:
    f.write(make_result.stdout)
    f.write(make_result.stderr)

if make_result.returncode != 0:
    with open(os.path.join(logs_dir, "tests.output"), "w") as f:
        f.write("Make step failed! More details in make.output.")
    sys.exit(1)

test_result = run_command(["make", "test"])

print(test_result.stdout)
with open(os.path.join(logs_dir, "tests.output"), "w") as f:
    f.write(test_result.stdout)
