#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
import os
import shlex
import subprocess
import logging
import cart_logparse
import cart_logtest
import socket

from apricot import TestWithoutServers
from general_utils import stop_processes
from write_host_file import write_host_file


class CartTest(TestWithoutServers):
    """Define a Cart test case."""

    def __init__(self, *args, **kwargs):
        """Initialize a CartTest object."""
        super().__init__(*args, **kwargs)
        self.stdout = logging.getLogger('avocado.test.stdout')
        self.progress_log = logging.getLogger("progress")
        self.module_init = False
        self.provider = None
        self.module = lambda *x: False

    def setUp(self):
        """Set up the test case."""
        super().setUp()
        self.set_other_env_vars()
        self.env = self.get_env()

    def tearDown(self):
        """Tear down the test case."""
        self.report_timeout()
        self._teardown_errors.extend(self.cleanup_processes())
        super().tearDown()

    @staticmethod
    def check_process(proc):
        """Check if a process is still running."""
        proc.poll()
        return proc.returncode is None

    @staticmethod
    def wait_process(proc, wait_time):
        """Wait for process to terminate."""
        i = wait_time
        return_code = None
        while i:
            proc.poll()
            return_code = proc.returncode
            if return_code is not None:
                break
            time.sleep(1)
            i = i - 1
        return return_code

    def cleanup_processes(self):
        """Clean up cart processes, in case avocado/apricot does not."""
        error_list = []
        localhost = socket.gethostname().split(".")[0:1]
        processes = r"'\<(crt_launch|orterun)\>'"
        retry_count = 0
        while retry_count < 2:
            result = stop_processes(localhost,
                                    processes,
                                    added_filter=r"'\<(grep|defunct)\>'")
            if 1 in result:
                self.log.info(
                    "Stopped '%s' processes on %s", processes, str(result[1]))
                retry_count += 1
            elif 0 in result:
                self.log.info("All '%s' processes have been stopped", processes)
                retry_count = 99
            else:
                error_list.append("Error detecting/stopping cart processes")
                retry_count = 99
        if retry_count == 2:
            error_list.append("Unable to stop cart processes!")
        return error_list

    @staticmethod
    def stop_process(proc):
        """Wait for process to terminate."""
        i = 60
        return_code = None
        while i:
            proc.poll()
            return_code = proc.returncode
            if return_code is not None:
                break
            time.sleep(1)
            i = i - 1

        if return_code is None:
            return_code = -1
            try:
                proc.terminate()
            except ValueError:
                proc.kill()

        return return_code

    def get_env(self):
        """Get the basic env setting in yaml."""
        env_CCSA = self.params.get("env", "/run/env_CRT_CTX_SHARE_ADDR/*/")
        test_name = self.params.get("name", "/run/tests/*/")

        if env_CCSA is not None:
            log_dir = "{}-{}".format(test_name, env_CCSA)
        else:
            # Ensure we don't try to + concat None and string
            env_CCSA = ""
            log_dir = "{}".format(test_name)

        # Write group attach info file(s) to HOME or DAOS_TEST_SHARED_DIR.
        # (It can't be '.' or cwd(), it must be some place writable.)
        daos_test_shared_dir = os.environ['HOME']
        if 'DAOS_TEST_SHARED_DIR' in os.environ:
            daos_test_shared_dir = os.environ['DAOS_TEST_SHARED_DIR']

        log_path = os.environ['DAOS_TEST_LOG_DIR']
        log_file = os.path.join(log_path, log_dir,
                                test_name + "_" + env_CCSA + "_cart.log")

        # Default env vars for orterun to None
        log_mask = None
        self.provider = None
        ofi_interface = None
        ofi_domain = None
        ofi_share_addr = None

        if "D_LOG_MASK" in os.environ:
            log_mask = os.environ.get("D_LOG_MASK")

        if "CRT_PHY_ADDR_STR" in os.environ:
            self.provider = os.environ.get("CRT_PHY_ADDR_STR")

        if "OFI_INTERFACE" in os.environ:
            ofi_interface = os.environ.get("OFI_INTERFACE")

        if "OFI_DOMAIN" in os.environ:
            ofi_domain = os.environ.get("OFI_DOMAIN")

        if "CRT_CTX_SHARE_ADDR" in os.environ:
            ofi_share_addr = os.environ.get("CRT_CTX_SHARE_ADDR")

        # Do not use the standard .log file extension, otherwise it'll get
        # removed (cleaned up for disk space savings) before we can archive it.
        log_filename = test_name + "_" + env_CCSA + "_output.orterun_log"
        output_filename_path = os.path.join(log_path, log_dir, log_filename)
        env = " --output-filename {!s}".format(output_filename_path)
        env += " -x D_LOG_FILE={!s}".format(log_file)
        env += " -x D_LOG_FILE_APPEND_PID=1"

        if log_mask is not None:
            env += " -x D_LOG_MASK={!s}".format(log_mask)

        if self.provider is not None:
            env += " -x CRT_PHY_ADDR_STR={!s}".format(self.provider)

        if ofi_interface is not None:
            env += " -x OFI_INTERFACE={!s}".format(ofi_interface)

        if ofi_domain is not None:
            env += " -x OFI_DOMAIN={!s}".format(ofi_domain)

        if ofi_share_addr is not None:
            env += " -x CRT_CTX_SHARE_ADDR={!s}".format(ofi_share_addr)

        env += " -x CRT_ATTACH_INFO_PATH={!s}".format(daos_test_shared_dir)

        self.log_path = log_path

        if not os.path.exists(log_path):
            os.makedirs(log_path)

        # If the logparser is being used, make sure the log directory is empty
        logparse = self.params.get("logparse", "/run/tests/*/")
        if logparse:
            for the_file in os.listdir(log_path):
                file_path = os.path.join(log_path, the_file)
                if os.path.isfile(file_path):
                    os.unlink(file_path)

        return env

    def get_srv_cnt(self, host):
        """Get server count for 'host' test yaml parameter.

        Args:
            host (str): test yaml parameter name

        Returns:
            int: length of the 'host' test yaml parameter

        """
        return len(self.params.get("{}".format(host), "/run/hosts/*/", []))

    @staticmethod
    def get_yaml_list_elem(param, index):
        """Get n-th element from YAML param.

        Args:
            param (str): yaml string or list value
            index (int): index into list or None (for a non-list param)

        Returns:
            value: n-th element of list or string value

        """
        if isinstance(param, list):
            return param[index]
        return param

    # pylint: disable=too-many-locals
    def build_cmd(self, env, host, **kwargs):
        """Build a command string."""
        tst_cmd = ""

        index = kwargs.get('index', None)

        tst_vgd = " valgrind --xml=yes " + \
                  "--xml-file={}/".format(self.log_path) + \
                  r"valgrind.%q\{PMIX_ID\}.memcheck " + \
                  "--fair-sched=try  --partial-loads-ok=yes " + \
                  "--leak-check=yes --gen-suppressions=all " + \
                  "--suppressions=../etc/memcheck-cart.supp " + \
                  "--show-reachable=yes "

        _tst_bin = self.params.get("{}_bin".format(host), "/run/tests/*/")
        _tst_arg = self.params.get("{}_arg".format(host), "/run/tests/*/")
        _tst_env = self.params.get("{}_env".format(host), "/run/tests/*/")
        _tst_slt = self.params.get("{}_slt".format(host), "/run/tests/*/")
        _tst_ctx = "16"
        if "{}_CRT_CTX_NUM".format(host) in os.environ:
            _tst_ctx = os.environ["{}_CRT_CTX_NUM".format(host)]

        # If the yaml parameter is a list, return the n-th element
        tst_bin = self.get_yaml_list_elem(_tst_bin, index)
        tst_arg = self.get_yaml_list_elem(_tst_arg, index)
        tst_env = self.get_yaml_list_elem(_tst_env, index)
        tst_slt = self.get_yaml_list_elem(_tst_slt, index)
        tst_ctx = self.get_yaml_list_elem(_tst_ctx, index)

        tst_host = self.params.get("{}".format(host), "/run/hosts/*/")
        tst_ppn = self.params.get("{}_ppn".format(host), "/run/tests/*/")
        logparse = self.params.get("logparse", "/run/tests/*/")

        # Write group attach info file(s) to HOME or DAOS_TEST_SHARED_DIR.
        # (It can't be '.' or cwd(), it must be some place writable.)
        daos_test_shared_dir = os.environ['HOME']
        if 'DAOS_TEST_SHARED_DIR' in os.environ:
            daos_test_shared_dir = os.environ['DAOS_TEST_SHARED_DIR']

        if tst_slt is not None:
            hostfile = write_host_file(tst_host, daos_test_shared_dir, tst_slt)
        else:
            hostfile = write_host_file(tst_host, daos_test_shared_dir, tst_ppn)

        mca_flags = "--mca btl self,tcp "

        if self.provider == "ofi+psm2":
            mca_flags += "--mca pml ob1 "

        tst_cmd = "{} {} -N {} --hostfile {} ".format(
            self.orterun, mca_flags, tst_ppn, hostfile)

        tst_cmd += env

        if tst_ctx is not None:
            tst_cmd += " -x CRT_CTX_NUM=" + tst_ctx

        if tst_env is not None:
            tst_cmd += " " + tst_env

        if logparse:
            tst_cmd += " -x D_LOG_FILE_APPEND_PID=1"

        tst_mod = os.getenv("CART_TEST_MODE", "native")
        if tst_mod == "memcheck":
            tst_cmd += tst_vgd

        if tst_bin is not None:
            tst_cmd += " " + tst_bin

        if tst_arg is not None:
            tst_cmd += " " + tst_arg

        return tst_cmd

    def launch_srv_cli_test(self, srvcmd, clicmd):
        """Launch a sever in the background and client in the foreground."""
        srv_rtn = self.launch_cmd_bg(srvcmd)

        # Verify the server is still running.
        if not self.check_process(srv_rtn):
            procrtn = self.stop_process(srv_rtn)
            self.fail("Server did not launch, return code {}".format(procrtn))

        cli_rtn = self.launch_test(clicmd, srv_rtn)
        srv_rtn = self.stop_process(srv_rtn)

        if srv_rtn:
            self.fail(
                "Failed, return codes client {} server {}".format(
                    cli_rtn, srv_rtn))

        return 0

    def launch_test(self, cmd, srv1=None, srv2=None):
        """Launch a test."""
        self.print("\nCMD : {}\n".format(cmd))
        self.print("\nENV : {}\n".format(os.environ))

        cmd = shlex.split(cmd)
        rtn = subprocess.call(cmd)

        if rtn:
            if srv1 is not None:
                self.stop_process(srv1)
            if srv2 is not None:
                self.stop_process(srv2)
            self.fail("Failed, return codes {}".format(rtn))

        return rtn

    def launch_cmd_bg(self, cmd):
        """Launch the given cmd in background."""
        self.print("\nCMD : {}\n".format(cmd))

        cmd = shlex.split(cmd)
        rtn = subprocess.Popen(cmd)

        if rtn is None:
            self.fail("Failed to start command\n")
            return -1

        return rtn

    def print(self, cmd):
        """Print the given cmd at runtime and stdout."""
        self.log.info(cmd)
        self.stdout.info(cmd)
        self.progress_log.info(cmd)

    def log_check(self):
        """Check log files for consistency."""
        logparse = self.params.get("logparse", "/run/tests/*/")
        if logparse is None or not logparse:
            return

        strict_test = False
        self.log.info("Parsing log path %s", self.log_path)
        if not os.path.exists(self.log_path):
            self.log.info("Path does not exist")
            return

        for filename in os.listdir(self.log_path):
            log_file = os.path.join(self.log_path, filename)
            if not os.path.isfile(log_file):
                self.log.info("File is a Directory. Skipping.... :%s", log_file)
                continue

            self.log.info("Parsing %s", log_file)
            cl = cart_logparse.LogIter(log_file)
            c_log_test = cart_logtest.LogTest(cl)
            c_log_test.check_log_file(strict_test)

    def set_other_env_vars(self):
        """Set env vars from yaml file."""
        default_env = self.params.get("default", "/run/ENV/")
        if default_env:
            for kv_pair in default_env:
                key = next(iter(kv_pair))
                if key is not None:
                    value = kv_pair[key]
                    self.log.info("Adding %s=%s to environment.", key, value)
                    os.environ[key] = value

            # For compatibility with cart tests, which set env vars in oretrun
            # command via -x options
            self.env = os.environ

    def unset_other_env_vars(self):
        """Unset env vars from yaml file."""
        default_env = self.params.get("default", "/run/ENV/")
        if default_env:
            for kv_pair in default_env:
                try:
                    key = kv_pair[0][0]
                    self.log.info("Removing key %s from environment.", key)
                    del os.environ[key]
                except IndexError:
                    pass
