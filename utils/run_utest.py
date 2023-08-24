#!/usr/bin/env python3
"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Test script for running all DAOS unit tests
"""
# pylint: disable=broad-except
import os
import sys
import json
import argparse
import shutil
import re
import subprocess  # nosec
import tempfile
import traceback
from junit_xml import TestSuite, TestCase
import yaml


def check_version():
    """Ensure python version is compatible"""
    if sys.version_info < (3, 6):
        print("Python version 3.6 or greater is required""")
        sys.exit(-1)


def write_xml_result(name, suite_junit):
    """Write an junit result"""
    with open(f"test_results/test_{name}.xml", "w", encoding='UTF-8') as file:
        TestSuite.to_file(file, [suite_junit], prettyprint=True)


def junit_name(memcheck):
    """Get name of junit test"""
    typestr = "native"
    if memcheck:
        typestr = "memcheck"
    return f"run_utest.py.{typestr}"


def setup_junit(memcheck):
    """Setup single junit for whole suite"""
    name = junit_name(memcheck)
    test = TestCase("run_utest", name)
    suite = TestSuite(name, [test])
    return (suite, test)


class BaseResults():
    """Keep track of test results"""
    def __init__(self):
        """Initializes the values"""
        self.results = {"tests": 0, "errors": 0, "failures": 0, "fail_msg": "", "error_msg": ""}

    def has_failures(self):
        """Return true if there are test failures or errors"""
        return self.results["errors"] or self.results["failures"]

    def merge(self, other):
        """Merge results from other into self"""
        self.results["tests"] += other.results["tests"]
        self.results["errors"] += other.results["errors"]
        self.results["failures"] += other.results["failures"]
        if other.results["failures"]:
            self.results["fail_msg"] += other.results["fail_msg"]
        if other.results["error_msg"]:
            self.results["error_msg"] += other.results["error_msg"]

    def add_test(self):
        """Add a test stat"""
        self.results["tests"] += 1

    def add_failure(self, fail_str):
        """Add a failure stat"""
        self.results["failures"] += 1
        self.results["fail_msg"] += f"{fail_str}\n"

    def add_error(self, error_str):
        """Add an error stat"""
        self.results["errors"] += 1
        self.results["error_msg"] += f"{error_str}\n"


class Results(BaseResults):
    """Keep track of test results to produce final report"""
    def __init__(self, memcheck):
        """Class to keep track of results"""
        super().__init__()
        self.name = junit_name(memcheck)
        self.test = TestCase("run_utest", self.name)
        self.suite = TestSuite(self.name, [self.test])

    def create_junit(self):
        """Create the junit output"""
        if os.environ.get("CMOCKA_XML_FILE", None) is None:
            return
        if self.results["failures"]:
            self.test.add_failure_info(message=f"{self.results['failures']} of "
                                               + f"{self.results['tests']} failed",
                                       output=self.results["fail_msg"])
        if self.results["errors"]:
            self.test.add_error_info(message=f"{self.results['errors']} of "
                                             + f"{self.results['tests']} failed",
                                     output=self.results["error_msg"])
        write_xml_result(self.name, self.suite)

    def print_results(self):
        """Print the output"""
        print(f"Ran {self.results['tests']} tests, {self.results['failures']} tests failed, "
              + f"{self.results['errors']} tests had errors")
        if self.results["failures"]:
            print("FAILURES:")
            print(self.results["fail_msg"])
        if self.results["errors"]:
            print("ERRORS:")
            print(self.results["error_msg"])


class ValgrindHelper():
    """Helper class to setup xml command"""
    @staticmethod
    def get_xml_name(name):
        """Get the xml file name"""
        return f"unit-test-{name}.memcheck.xml"

    @staticmethod
    def get_supp(base):
        """Get suppression file"""
        return os.path.join(base, 'utils', 'test_memcheck.supp')

    @staticmethod
    def setup_cmd(base, cmd, name):
        """Return a new command using valgrind"""
        cmd_prefix = ["valgrind", "--leak-check=full", "--show-reachable=yes", "--num-callers=20",
                      "--error-limit=no", "--fair-sched=try",
                      f"--suppressions={ValgrindHelper.get_supp(base)}",
                      "--gen-suppressions=all", "--error-exitcode=42", "--xml=yes",
                      f"--xml-file={ValgrindHelper.get_xml_name(name)}"]
        return cmd_prefix + cmd


def run_cmd(cmd, output_log=None, env=None):
    """Run a command"""
    # capture_output is only available in 3.7+
    if output_log:
        with open(output_log, "w", encoding="UTF-8") as output:
            print(f"RUNNING COMMAND {' '.join(cmd)}\n    Log: {output_log}")
            ret = subprocess.run(cmd, check=False, env=env, stdout=output,
                                 stderr=subprocess.STDOUT)
    else:
        print(f"RUNNING COMMAND {' '.join(cmd)}")
        ret = subprocess.run(cmd, check=False, env=env)
    print(f'rc is {ret.returncode}')
    return ret.returncode


class TestSkipped(Exception):
    """Used to indicate a test is skipped"""


class SuiteSkipped(Exception):
    """Used to indicate a test is skipped"""


class SuiteConfigError(Exception):
    """Used to indicate a test is skipped"""


def change_ownership(fname, _arg):
    """For files created by sudo process, change the permissions and ownership"""
    if not os.path.isfile(fname):
        print(f"{fname} not a file")
        return
    uname = os.getlogin()
    print(f"chown {fname}")
    run_cmd(["sudo", "-E", "chgrp", uname, fname])
    run_cmd(["sudo", "-E", "chown", uname, fname])


def process_cmocka(fname, suite_name):
    """For files created by sudo process, change the permissions and ownership"""
    dirname = os.path.dirname(fname)
    basename = os.path.basename(fname)

    if re.search("run_utest.py", basename):
        return
    if re.search("UTEST_", basename):
        return
    print(f"Processing cmocka {basename}")
    target = os.path.join(dirname, f"UTEST_{suite_name}.{basename}")
    suite = None
    with open(target, "w", encoding='UTF-8') as outfile:
        with open(fname, "r", encoding='UTF-8') as infile:
            for line in infile.readlines():
                line = line.strip()
                if suite is None:
                    match = re.search(r"testsuite.*name=\"(\w+)", line)
                    if match:
                        suite = match.group(1)
                else:
                    match = re.search("^(.*case )name(.*$)", line)
                    if match:
                        outfile.write(f"{match.group(1)}classname=\"UTEST_{suite_name}.{suite}\""
                                      + f" name{match.group(2)}\n")
                        continue
                    match = re.search("^(.*case classname=\")(.*$)", line)
                    if match:
                        outfile.write(f"{match.group(1)}UTEST_{suite_name}.{match.group(2)}\n")
                        continue
                outfile.write(f"{line}\n")
    os.unlink(fname)


def for_each_file(path, operate, arg, ext=None):
    """Find cmocka files, run some operation on each"""
    for file in os.listdir(path):
        full_path = os.path.join(path, file)
        if not os.path.isfile(full_path):
            continue
        if ext is None or ext == os.path.splitext(file)[-1]:
            operate(full_path, arg)


class AIO():
    """Handle AIO specific setup and teardown"""
    def __init__(self, mount, device=None):
        """Initialize an AIO device"""
        self.config_name = os.path.join(mount, "daos_nvme.conf")
        self.device = device
        self.current_info = {}
        self.fname = None
        self.ready = False

    def initialize(self):
        """Initialize global AIO information"""
        if self.ready:
            return
        if self.device is not None:
            self.fname = self.device
            print(f"Using aio device {self.fname}")
            return
        (_fd, self.fname) = tempfile.mkstemp(prefix="aio_", dir="/tmp")
        self.ready = True
        print(f"Using aio file {self.fname}")

    def create_config(self, name):
        """Create the AIO config file"""

        contents = f"""
{{
  "daos_data": {{
    "config": []
  }},
  "subsystems": [
    {{
      "subsystem": "bdev",
      "config": [
        {{
          "params": {{
            "bdev_io_pool_size": 65536,
            "bdev_io_cache_size": 256
          }},
          "method": "bdev_set_options"
        }},
        {{
          "params": {{
            "retry_count": 4,
            "timeout_us": 0,
            "nvme_adminq_poll_period_us": 100000,
            "action_on_timeout": "none",
            "nvme_ioq_poll_period_us": 0
          }},
          "method": "bdev_nvme_set_options"
        }},
        {{
          "params": {{
            "enable": false,
            "period_us": 0
          }},
          "method": "bdev_nvme_set_hotplug"
        }},
        {{
          "params": {{
            "block_size": 4096,
            "name": "{name}",
            "filename": "{self.fname}"
          }},
          "method": "bdev_aio_create"
        }}
      ]
    }}
  ]
}}
"""
        with open(self.config_name, "w", encoding='UTF-8') as config_file:
            config_file.write(contents)

    def prepare_test(self, name="AIO_1", min_size=4):
        """Prepare AIO for a test, min_size in GB. Erase 4K header if device exists (no truncate
        opt makes dd behavior consistent across device disks and files enabling unit tests to be
        run locally with /dev/vdb filt).
        """
        if self.device is None:
            run_cmd(["dd", "if=/dev/zero", f"of={self.fname}", "bs=1G", f"count={min_size}"])
        else:
            run_cmd(["sudo", "-E", "dd", "if=/dev/zero", f"of={self.fname}", "bs=4K", "count=1",
                     "conv=notrunc"])
        self.create_config(name)

    def finalize_test(self):
        """Finalize AIO for a test"""
        if self.device is None:
            os.unlink(self.fname)
        if os.path.exists(self.config_name):
            os.unlink(self.config_name)

    def finalize(self):
        """Finalize global AIO information"""
        if self.ready and self.device is None:
            if os.path.exists(self.fname):
                os.unlink(self.fname)
        if os.path.exists(self.config_name):
            os.unlink(self.config_name)


class Test():
    """Define a test"""

    test_num = 1

    def __init__(self, config, path_info, args):
        """Initialize a test"""
        self.cmd = self.subst(config["cmd"], config.get("replace_path", {}), path_info)
        self.env = os.environ.copy()
        self.last = []
        env_vars = config.get("env_vars", {})
        if env_vars:
            self.env.update(env_vars)
        self.warn_if_missing = config.get("warn_if_missing", None)
        self.aio = {"aio": config.get("aio", None),
                    "size": config.get("size", 4)}
        if self.filter(args.test_filter):
            print(f"Filtered test  {' '.join(self.cmd)}")
            raise TestSkipped()

        self.path_info = path_info
        name = '-'.join(self.cmd).replace(';', '-').replace('/', '-') + f"_{Test.test_num}"
        self.name = name.replace(' ', '-')
        self.env["D_LOG_FILE"] = os.path.join(self.log_dir(), "daos.log")
        Test.test_num = Test.test_num + 1

        if self.needs_aio():
            self.env["VOS_BDEV_CLASS"] = "AIO"

    def log_dir(self):
        """Return the log directory"""
        return os.path.join(self.path_info["LOG_DIR"], self.name)

    def root_dir(self):
        """Return the log directory"""
        return self.path_info["DAOS_BASE"]

    def cmocka_dir(self):
        """Return the log directory"""
        return os.path.join(self.root_dir(), "test_results")

    def subst(self, cmd, replacements, path_info):
        """Make any substitutions for variables in path_info"""
        if not replacements:
            return cmd

        new_cmd = []
        for entry in cmd:
            for key, value in replacements.items():
                try:
                    path = path_info[value]
                except KeyError as exception:
                    print(f"Invalid path_info key {value} in configuration for {self.name}")
                    raise exception
                entry = entry.replace(key, path)
            new_cmd.append(entry)

        return new_cmd

    def filter(self, test_filter):
        """Determine if the test should run"""
        if test_filter is None:
            return False

        if re.search(test_filter, ' '.join(self.cmd), re.IGNORECASE):
            return False

        return True

    def needs_aio(self):
        """Returns true if the test uses aio"""
        return self.aio["aio"] is not None

    def get_last(self):
        """Retrieve the last command run"""
        return self.last

    def setup(self, base, aio):
        """Setup the test"""
        fname = os.path.join(base, self.cmd[0])
        if not os.path.isfile(fname):
            if self.warn_if_missing:
                print(f"{fname} is not found\n{self.warn_if_missing}")
                return False
            raise FileNotFoundError(fname)
        if self.needs_aio():
            aio.prepare_test(self.aio["aio"], self.aio["size"])

        os.makedirs(self.log_dir(), exist_ok=True)
        for file in os.listdir():
            fname = os.path.join(self.log_dir(), file)
            if os.path.isfile(fname):
                print(f"Removing old log {fname}")
                os.unlink(fname)

        return True

    def run(self, base, memcheck, sudo):
        """Run the test"""
        cmd = [os.path.join(base, self.cmd[0])] + self.cmd[1:]
        if memcheck:
            if os.path.splitext(cmd[0])[-1] in [".sh", ".py"]:
                self.env.update({"USE_VALGRIND": "memcheck",
                                 "VALGRIND_SUPP": ValgrindHelper.get_supp(self.root_dir())})
            else:
                cmd = ValgrindHelper.setup_cmd(self.root_dir(), cmd, self.name)
        if sudo:
            new_cmd = ["sudo", "-E"]
            new_cmd.extend(cmd)
            cmd = new_cmd
        self.last = cmd

        output_log = os.path.join(self.log_dir(), "output.log")
        retval = run_cmd(cmd, output_log=output_log, env=self.env)

        return retval

    def teardown(self, suite_name, aio, sudo):
        """Teardown the test"""
        if self.needs_aio():
            aio.finalize_test()
        if sudo:
            change_ownership(ValgrindHelper.get_xml_name(self.name), None)
            for_each_file(self.log_dir(), change_ownership, None)
            for_each_file(self.cmocka_dir(), change_ownership, None, ".xml")
        for_each_file(self.cmocka_dir(), process_cmocka, suite_name, ".xml")
        self.remove_empty_files(self.log_dir())

    def remove_empty_files(self, log_dir):
        """Remove empty log files, they are useless"""
        if not os.path.isdir(log_dir):
            return
        print(f"Processing logs for {self.name}")
        for log in os.listdir(log_dir):
            fname = os.path.join(log_dir, log)
            if not os.path.isfile(fname):
                continue
            if os.path.getsize(fname) == 0:
                os.unlink(fname)
            print(f"   produced {fname}")


class Suite():
    """Define a suite"""
    def __init__(self, path_info, config, args):
        """Initialize a test suite"""
        self.name = config["name"]
        val = config.get("base", "DAOS_BASE")
        self.base = path_info.get(val, None)
        if self.base is None:
            print(f"{val} not found in path_info")
            raise SuiteConfigError()
        self.sudo = config.get("sudo", None)
        self.memcheck = config.get("memcheck", True)
        self.tests = []
        self.has_aio = False

        if self.skip(path_info, config, args):
            raise SuiteSkipped()

        for test in config["tests"]:
            try:
                real_test = Test(test, path_info, args)
            except TestSkipped:
                continue
            if real_test.needs_aio():
                self.has_aio = True
            self.tests.append(real_test)
        if not self.tests:
            print(f"Skipped  suite {self.name}, no tests matched filters")
            raise SuiteSkipped()

    def skip(self, path_info, config, args):
        """Check all of the conditions for skipping the suite"""
        required = config.get("required_src", [])

        for file in required:
            fname = os.path.join(path_info["DAOS_BASE"], file)
            if not os.path.isfile(fname):
                print(f"Skipped  suite {self.name}, not enabled on branch")
                return True

        if self.filter(args.suite_filter):
            print(f"Filtered suite {self.name}")
            return True

        if self.sudo:
            if args.sudo == 'no':
                print(f"Skipped  suite {self.name}, requires sudo")
                return True
        elif args.sudo == 'only':
            print(f"Skipped  suite {self.name}, doesn't require sudo")
            return True

        if args.memcheck and not self.memcheck:
            print(f"Skipped  suite {self.name}, valgrind not supported")
            raise SuiteSkipped()
        return False

    def needs_aio(self):
        """The suite needs to use aio"""
        return self.has_aio

    def filter(self, suite_filter):
        """Determine if suite should run"""
        if suite_filter is None:
            return False

        if re.search(suite_filter, self.name, re.IGNORECASE):
            return False

        return True

    def run_suite(self, args, aio):
        """Run the test suite"""
        print(f"\nRunning suite {self.name}")
        results = BaseResults()

        if self.needs_aio() and aio is not None:
            aio.initialize()
        for test in self.tests:
            run_test = True
            ret = 0
            try:
                if not test.setup(self.base, aio):
                    continue
            except Exception:
                results.add_error(f"{traceback.format_exc()}")
                run_test = False
            results.add_test()
            if run_test:
                try:
                    ret = test.run(self.base, args.memcheck, self.sudo)
                    if ret != 0:
                        results.add_failure(f"{' '.join(test.get_last())} failed: ret={ret} "
                                            + f"logs={test.log_dir()}")
                except Exception:
                    results.add_error(f"{traceback.format_exc()}")
                    ret = 1  # prevent reporting errors on teardown too
            try:
                test.teardown(self.name, aio, self.sudo)
            except Exception:
                if not run_test or ret != 0:
                    pass
                results.add_error(f"{traceback.format_exc()}")

        if self.needs_aio() and aio is not None:
            aio.finalize()
        return results


def run_suites(args, suites, results, aio):
    """Run a set of suites"""
    for suite in suites:
        results.merge(suite.run_suite(args, aio))


def move_codecov(base):
    """Move any code coverage results"""
    try:
        target = "/tmp/test.cov"
        if os.path.isfile(target):
            os.unlink(target)
        src = os.path.join(base, "test.cov")
        if os.path.isfile(src):
            print(f"Moving {src} to {target}")
            shutil.move(src, target)
    except Exception:
        print("Exception trying to copy test.cov")
        traceback.print_exc()


def get_args():
    """Parse the arguments"""
    parser = argparse.ArgumentParser(description='Run DAOS unit tests')
    parser.add_argument('--memcheck', action='store_true', help='Run tests with Valgrind memcheck')
    parser.add_argument('--test_filter', default=None,
                        help='Regular expression to select tests to run')
    parser.add_argument('--suite_filter', default=None,
                        help='Regular expression to select suites to run')
    parser.add_argument('--no-fail-on-error', action='store_true',
                        help='Disable non-zero return code on failure')
    parser.add_argument('--sudo', choices=['yes', 'only', 'no'], default='yes',
                        help='How to handle tests requiring sudo')
    parser.add_argument('--bdev', default=None,
                        help="Device to use for AIO, will create file by default")
    parser.add_argument('--log_dir', default="/tmp/daos_utest",
                        help="Path to store test logs")
    return parser.parse_args()


def get_path_info(args):
    """Retrieve the build variables"""
    script_dir = os.path.dirname(os.path.realpath(__file__))
    daos_base = os.path.realpath(os.path.join(script_dir, '..'))
    build_vars_file = os.path.join(daos_base, '.build_vars.json')
    path_info = {"DAOS_BASE": daos_base,
                 "UTEST_YAML": os.path.join(daos_base, "utils", "utest.yaml"),
                 "MOUNT_DIR": "/mnt/daos",
                 "LOG_DIR": args.log_dir}
    try:
        with open(build_vars_file, "r", encoding="UTF-8") as vars_file:
            build_vars = json.load(vars_file)
        path_info.update(build_vars)
    except ValueError as error:
        print(f"Could not load json build info {build_vars_file}, have you built DAOS")
        print(f"{error}")
        sys.exit(-1)

    return path_info


def main():
    """Run the the core tests"""
    args = get_args()
    path_info = get_path_info(args)

    os.makedirs(os.path.join(path_info["DAOS_BASE"], "test_results"), exist_ok=True)

    aio = None

    if args.sudo in ['yes', 'only']:
        aio = AIO(path_info["MOUNT_DIR"], args.bdev)

    suites = []
    with open(path_info["UTEST_YAML"], "r", encoding="UTF-8") as file:
        all_suites = yaml.safe_load(file)

    for suite_yaml in all_suites:
        try:
            real_suite = Suite(path_info, suite_yaml, args)
        except SuiteSkipped:
            continue
        except SuiteConfigError as exception:
            print(f"Error processing {path_info['UTEST_YAML']} {exception}")
            raise exception
        except (KeyError, ValueError) as exception:
            print(f"Error processing {path_info['UTEST_YAML']}")
            raise exception
        suites.append(real_suite)
    results = Results(args.memcheck)
    run_suites(args, suites, results, aio=aio)

    results.print_results()

    results.create_junit()

    move_codecov(path_info["DAOS_BASE"])

    if args.no_fail_on_error:
        return

    if results.has_failures():
        sys.exit(-1)


if __name__ == '__main__':
    check_version()
    main()
