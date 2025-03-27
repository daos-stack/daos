#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
# SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved

import argparse
import builtins
import os
import sys

import yaml

import pytest
from junitparser import JUnitXml
from pytest import ExitCode


def get_option_longform(option_name, option_params):
    '''
        get the long form command line option name of an option
    '''
    return option_params.get("longform", "--" + option_name.replace("_", "-"))

def get_ubertest_test_type(fabtests_testsets):
    test_list = fabtests_testsets.split(",")

    for test in test_list:
        if test == "quick" or test == "ubertest_quick" or test == "ubertest":
            return "quick"

        if test == "all" or test == "ubertest_all":
            return "all"

        if test == "verify" or test == "ubertest_verify":
            return "verify"

    return None

def fabtests_testsets_to_pytest_markers(fabtests_testsets, run_mode=None):
    if run_mode:
        assert run_mode in ["serial", "parallel"]

    test_set = set()
    test_list = fabtests_testsets.split(",")

    # use set() to remove duplicate test set
    for test in test_list:
        if test == "quick":
            test_set.add("unit")
            test_set.add("functional")
            test_set.add("short")
            test_set.add("ubertest_quick")
        elif test =="ubertest":
            test_set.add("ubertest_quick")
        elif test == "all":
            test_set.add("unit")
            test_set.add("functional")
            test_set.add("standard")
            test_set.add("multinode")
            test_set.add("ubertest_all")
        elif test == "verify":
            test_set.add("ubertest_verify")
        else:
            test_set.add(test)

    markers = None
    for test in test_set:
        if markers is None:
            markers = test[:]
        else:
            markers += " or " + test

    if run_mode:
        if run_mode == "serial":
            markers = "(" + markers + ") and (serial)"
        else:
            assert run_mode == "parallel"
            markers = "(" + markers + ") and (not serial)"

    return markers

def get_default_exclusion_file(fabtests_args):
    test_configs_dir = os.path.abspath(os.path.join(get_pytest_root_dir(), "..", "test_configs"))
    exclusion_file = os.path.join(test_configs_dir, fabtests_args.provider,
                                  fabtests_args.provider + ".exclude")
    if not os.path.exists(exclusion_file):
        return None

    return exclusion_file

def get_default_ubertest_config_file(fabtests_args):
    test_configs_dir = os.path.abspath(os.path.join(get_pytest_root_dir(), "..", "test_configs"))
    provider = fabtests_args.provider
    if provider.find(";") != -1:
        core,util = fabtests_args.provider.split(";")
        cfg_file = os.path.join(test_configs_dir, util, core + ".test")
    else:
        core = fabtests_args.provider
        ubertest_test_type = get_ubertest_test_type(fabtests_args.testsets)
        if not ubertest_test_type:
            return None

        cfg_file = os.path.join(test_configs_dir, core, ubertest_test_type + ".test")

    if not os.path.exists(cfg_file):
        return None

    return cfg_file

def add_common_arguments(parser, shared_options):
    for option_name in shared_options.keys():
        option_params = shared_options[option_name]
        option_longform = get_option_longform(option_name, option_params)
        option_shortform = option_params.get("shortform")
        option_type = option_params["type"]
        option_helpmsg = option_params["help"]
        option_default = option_params.get("default")
        if option_type == "int" and not (option_default is None):
            option_default = int(option_default)

        if option_shortform:
            forms = [option_shortform, option_longform]
        else:
            forms = [option_longform]

        if option_type == "bool" or option_type == "boolean":
            parser.add_argument(*forms,
                                dest=option_name, action="store_true",
                                help=option_helpmsg, default=option_default)
        else:
            assert option_type == "str" or option_type == "int"
            parser.add_argument(*forms,
                                dest=option_name, type=getattr(builtins, option_type),
                                help=option_helpmsg, default=option_default)

def fabtests_args_to_pytest_args(fabtests_args, shared_options, run_mode):
    pytest_args = []

    if run_mode == "parallel":
        pytest_args.append("-n")
        pytest_args.append(str(fabtests_args.nworkers))

    pytest_args.append("--provider=" + fabtests_args.provider)
    pytest_args.append("--server-id=" + fabtests_args.server_id)
    pytest_args.append("--client-id=" + fabtests_args.client_id)

    # -v make pytest to print 1 line for each test
    pytest_args.append("-v")

    pytest_verbose_options = {
            0 : "-rN",      # print no extra information
            1 : "-rfE",     # print extra information for failed test(s)
            2 : "-rfEsx",   # print extra information for failed/skipped test(s)
            3 : "-rA",      # print extra information for all test(s) (failed/skipped/passed)
        }

    pytest_args.append(pytest_verbose_options[fabtests_args.verbose])

    verbose_fail = fabtests_args.verbose > 0
    if verbose_fail:
        # Use short python trace back because it show captured stdout of failed tests
        pytest_args.append("--tb=short")
    else:
        pytest_args.append("--tb=no")

    markers = fabtests_testsets_to_pytest_markers(fabtests_args.testsets, run_mode)
    pytest_args.append("-m")
    pytest_args.append(markers)

    if fabtests_args.expression:
        pytest_args.append("-k")
        pytest_args.append(fabtests_args.expression)

    if fabtests_args.html:
        pytest_args.append("--html")
        pytest_args.append(os.path.abspath(fabtests_args.html))
        pytest_args.append("--self-contained-html")

    if fabtests_args.junit_xml:
        pytest_args.append("--junit-xml")
        file_name = os.path.abspath(fabtests_args.junit_xml)
        if run_mode:
            file_name += "." + run_mode
        pytest_args.append(file_name)
        if fabtests_args.junit_logging:
            pytest_args.append("-o")
            pytest_args.append("junit_logging=" + fabtests_args.junit_logging)

    # add options shared between runfabtests.py and libfabric pytest
    for option_name in shared_options.keys():
        option_params = shared_options[option_name]
        option_longform = get_option_longform(option_name, option_params)
        option_type = option_params["type"]
 
        if not hasattr(fabtests_args, option_name):
            continue

        option_value = getattr(fabtests_args, option_name)
        if (option_value is None):
            continue

        if option_type == "bool" or option_type == "boolean":
            assert option_value
            pytest_args.append(option_longform)
        else:
            assert option_type == "str" or option_type == "int"
            pytest_args.append(option_longform + "=" + str(option_value))

    if not hasattr(fabtests_args, "exclusion_file") or not fabtests_args.exclusion_file:
        default_exclusion_file = get_default_exclusion_file(fabtests_args)
        if default_exclusion_file:
            pytest_args.append("--exclusion-file=" + default_exclusion_file)

    if not hasattr(fabtests_args, "ubertest_config_file") or not fabtests_args.ubertest_config_file:
        default_ubertest_config_file = get_default_ubertest_config_file(fabtests_args)
        if default_ubertest_config_file:
            pytest_args.append("--ubertest-config-file=" + default_ubertest_config_file)

    return pytest_args

def get_pytest_root_dir():
    '''
        find the pytest root directory according the location of runfabtests.py
    '''
    script_path = os.path.abspath(sys.argv[0])
    script_dir = os.path.dirname(script_path)
    if os.path.basename(script_dir) == "bin":
        # runfabtests.py is part of a fabtests installation
        pytest_root_dir = os.path.abspath(os.path.join(script_dir, "..", "share", "fabtests", "pytest"))
    elif os.path.basename(script_dir) == "scripts":
        # runfabtests.py is part of a fabtests source code
        pytest_root_dir = os.path.abspath(os.path.join(script_dir, "..", "pytest"))
    else:
        raise RuntimeError("Error: runfabtests.py is under directory {}, "
                "which is neither part of fabtests installation "
                "nor part of fabetsts source code".format(script_dir))

    if not os.path.exists(pytest_root_dir):
        raise RuntimeError("Deduced pytest root directory {} does not exist!".format(pytest_root_dir))

    return pytest_root_dir

def get_pytest_relative_case_dir(fabtests_args, pytest_root_dir):
    '''
        the directory that contains test cases, relative to pytest_root_dir
    '''
    # provider's own test directory (if exists) overrides default
    pytest_case_dir = os.path.join(pytest_root_dir, fabtests_args.provider)
    if os.path.exists(pytest_case_dir):
        return fabtests_args.provider

    assert os.path.exists(os.path.join(pytest_root_dir, "default"))
    return "default"


def run(fabtests_args, shared_options, run_mode):
    prev_cwd = os.getcwd()
    pytest_root_dir = get_pytest_root_dir()

    pytest_args = fabtests_args_to_pytest_args(fabtests_args, shared_options, run_mode)
    pytest_args.append(get_pytest_relative_case_dir(fabtests_args, pytest_root_dir))

    pytest_command = "cd " + pytest_root_dir + "; pytest"
    for arg in pytest_args:
        if arg.find(' ') != -1:
            arg = "'" + arg + "'"
        pytest_command += " " + arg
    print(pytest_command)

    # actually running tests

    os.chdir(pytest_root_dir)
    status = pytest.main(pytest_args)
    os.chdir(prev_cwd)
    return status


def main():
    pytest_root_dir = get_pytest_root_dir()

    # pytest/options.yaml contains the definition of a list of options that are
    # shared between runfabtests.py and pytest
    option_yaml = os.path.join(pytest_root_dir, "options.yaml")
    if not os.path.exists(option_yaml):
        print("Error: option definition yaml file {} not found!".format(option_yaml))
        exit(1)

    shared_options = yaml.safe_load(open(option_yaml))

    parser = argparse.ArgumentParser(description="libfabric integration test runner")

    parser.add_argument("provider", type=str, help="libfabric provider")
    parser.add_argument("server_id", type=str, help="server ip or hostname")
    parser.add_argument("client_id", type=str, help="client ip or hostname")
    parser.add_argument("-t", dest="testsets", type=str, default="quick",
                        help="test set(s): all,quick,unit,functional,standard,short,ubertest,cuda_memory,neuron_memory (default quick)")
    parser.add_argument("-v", dest="verbose", action="count", default=0,
                        help="verbosity level"
                             "-v: print extra info for failed test(s)"
                             "-vv: print extra info of failed/skipped test(s)"
                             "-vvv: print extra info of failed/skipped/passed test(s)")
    parser.add_argument("--expression", type=str,
                        help="only run tests which match the given substring expression.")
    parser.add_argument("--html", type=str, help="path to generated html report")
    parser.add_argument("--junit-xml", type=str, help="path to generated junit xml report")
    parser.add_argument("--junit-logging", choices=['no', 'log', 'system-out', 'system-err', 'out-err', 'all'], type=str,
                        help="Write captured log messages to JUnit report")
    parser.add_argument("--nworkers", type=int, default=8, help="Number of parallel test workers. Defaut is 8.")

    add_common_arguments(parser, shared_options)

    fabtests_args = parser.parse_args()
    if fabtests_args.provider not in ["efa", "shm"] and fabtests_args.nworkers > 1:
        print("only efa and shm provider support parallelized tests. Setting nworkers to 1 ....")
        fabtests_args.nworkers = 1

    if fabtests_args.html:
        print("html cannot be generated under parallel mode. Setting nworkers to 1 ....")
        fabtests_args.nworkers = 1

    if fabtests_args.nworkers == 1:
        exit(run(fabtests_args, shared_options, None))
    else:
        print("Running parallelable tests in parallel mode")
        parallel_status = run(fabtests_args, shared_options, "parallel")

        print("Running other tests in serial mode")
        serial_status = run(fabtests_args, shared_options, "serial")

        if fabtests_args.junit_xml:
            merged_xml = JUnitXml.fromfile(f'{fabtests_args.junit_xml}.parallel') + JUnitXml.fromfile(f'{fabtests_args.junit_xml}.serial')
            merged_xml.write(f'{fabtests_args.junit_xml}')
            os.unlink(fabtests_args.junit_xml + ".parallel")
            os.unlink(fabtests_args.junit_xml + ".serial")

        # Still return success when no tests are collected.
        if parallel_status not in [ExitCode.OK, ExitCode.NO_TESTS_COLLECTED]:
            exit(parallel_status)

        if serial_status not in [ExitCode.OK, ExitCode.NO_TESTS_COLLECTED]:
            exit(serial_status)

        exit(0)

main()
