#!/usr/bin/env python3

#
# Copyright(c) 2012-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

# ALL PATHS SHOULD BE ENDED WITH "/" CHARACTER

MAIN_DIRECTORY_OF_TESTED_PROJECT = "../../../"

MAIN_DIRECTORY_OF_ENV_FILES = MAIN_DIRECTORY_OF_TESTED_PROJECT + "env/posix/"

MAIN_DIRECTORY_OF_UNIT_TESTS = "../tests/"

# Paths to all directories, in which tests are stored. All paths should be relative to
# MAIN_DIRECTORY_OF_UNIT_TESTS
DIRECTORIES_WITH_TESTS_LIST = ["cleaning/", "metadata/", "mngt/", "concurrency/", "engine/",
                               "ocf_space.c/", "ocf_lru.c/", "utils/", "promotion/"]

# Paths to all directories containing files with sources. All paths should be relative to
# MAIN_DIRECTORY_OF_TESTED_PROJECT
DIRECTORIES_TO_INCLUDE_FROM_PROJECT_LIST = ["src/", "src/cleaning/", "src/engine/", "src/metadata/",
                                            "src/eviction/", "src/mngt/", "src/concurrency/",
                                            "src/utils/", "inc/", "src/promotion/",
                                            "src/promotion/nhit/"]

# Paths to all directories from directory with tests, which should also be included
DIRECTORIES_TO_INCLUDE_FROM_UT_LIST = ["ocf_env/"]

# Paths to include, required by cmake, cmocka, cunit
FRAMEWORK_DIRECTORIES_TO_INCLUDE_LIST = ["${CMOCKA_PUBLIC_INCLUDE_DIRS}", "${CMAKE_BINARY_DIR}",
                                         "${CMAKE_CURRENT_SOURCE_DIR}"]

# Path to directory containing all sources after preprocessing. Should be relative to
# MAIN_DIRECTORY_OF_UNIT_TESTS
PREPROCESSED_SOURCES_REPOSITORY = "preprocessed_sources_repository/"

# Path to directory containing all sources after removing unneeded functions and cmake files for
# tests
SOURCES_TO_TEST_REPOSITORY = "sources_to_test_repository/"

# List of includes.
# Directories will be recursively copied to given destinations in directory with tests.
# key - destination in dir with tests
# value - path in tested project to dir which should be copied
INCLUDES_TO_COPY_DICT = {'ocf_env/ocf/': "inc/"}
