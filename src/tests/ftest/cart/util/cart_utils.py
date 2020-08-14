#!/usr/bin/python
'''
  (C) Copyright 2018-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function
#pylint: disable=broad-except

import time
from distutils.spawn import find_executable
import os
import random
# MPI environment module needs this
#pylint: disable=unused-import
import re
#pylint: enable=unused-import
import shlex
import subprocess
import logging
import cart_logparse
import cart_logtest

import avocado

import os, fnmatch

class CartUtils():
    """CartUtils Class"""

    def __init__(self):
        """ CartUtils init """
        self.stdout = logging.getLogger('avocado.test.stdout')
        self.progress_log = logging.getLogger("progress")
        self.module_init = False
        self.provider = None
        self.module = lambda *x: False
        self.cart_logs_dumped_already = False

    @staticmethod
    def write_host_file(hostlist, slots=1):
        print("Entering write_host_file")
        """ write out a hostfile suitable for orterun """

        unique = random.randint(1, 100000)

        path = './hostfile'

        if not os.path.exists(path):
            os.makedirs(path)
        hostfile = path + "/hostfile" + str(unique)

        if hostlist is None:
            raise ValueError("host list parameter must be provided.")
        hostfile_handle = open(hostfile, 'w')

        for host in hostlist:
            if slots is None:
                print("<<{}>>".format(slots))
                hostfile_handle.write("{0}\n".format(host))
            else:
                print("<<{}>>".format(slots))
                hostfile_handle.write("{0} slots={1}\n".format(host, slots))
        hostfile_handle.close()
        return hostfile

    @staticmethod
    def check_process(proc):
        print("Entering check_process")
        """ check if a process is still running"""
        proc.poll()
        procrtn = proc.returncode
        if procrtn is None:
            return True
        return False

    @staticmethod
    def wait_process(proc, wait_time):
        print("Entering wait_process")
        """ wait for process to terminate """
        i = wait_time
        procrtn = None
        while i:
            proc.poll()
            procrtn = proc.returncode
            if procrtn is not None:
                break
            else:
                time.sleep(1)
                i = i - 1

        return procrtn

    @staticmethod
    def stop_process(proc, self):
        print("Entering stop_process")
        """ wait for process to terminate """
        i = 60
        procrtn = None
        while i:
            proc.poll()
            procrtn = proc.returncode
            if procrtn is not None:
                break
            else:
                time.sleep(1)
                i = i - 1

        if procrtn is None:
            procrtn = -1
            try:
                proc.terminate()
            except Exception:
                proc.kill()

        # Putting this in the tearDown section of the test .py file, might
        # not be enough.  Copy logs over when test procs are reaped.
        self.dump_cart_logs()

        return procrtn

    def get_env(self, cartobj):
        self.stdout.info("Entering get_env")
        """ return basic env setting in yaml """
        env_CCSA = cartobj.params.get("env", "/run/env_CRT_CTX_SHARE_ADDR/*/")
        test_name = cartobj.params.get("name", "/run/tests/*/")
        host_cfg = cartobj.params.get("config", "/run/hosts/*/")

        if env_CCSA is not None:
            log_dir = "{}-{}".format(test_name, env_CCSA)
        else:
            log_dir = "{}".format(test_name)

        log_path = os.path.join("testLogs", log_dir)
        log_file = os.path.join(log_path, "cart.log")

        log_mask = cartobj.params.get("D_LOG_MASK", "/run/defaultENV/")
        self.provider = cartobj.params.get("CRT_PHY_ADDR_STR",
                                          "/run/defaultENV/")
        ofi_interface = cartobj.params.get("OFI_INTERFACE", "/run/defaultENV/")
        ofi_domain = cartobj.params.get("OFI_DOMAIN", "/run/defaultENV/")
        ofi_share_addr = cartobj.params.get("CRT_CTX_SHARE_ADDR",
                                            "/run/env_CRT_CTX_SHARE_ADDR/*/")

        env = " --output-filename {!s}".format(log_path)
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

        cartobj.log_path = log_path

        if not os.path.exists(log_path):
            os.makedirs(log_path)

        # If the logparser is being used, make sure the log directory is empty
        logparse = cartobj.params.get("logparse", "/run/tests/*/")
        if logparse:
            for the_file in os.listdir(log_path):
                file_path = os.path.join(log_path, the_file)
                if os.path.isfile(file_path):
                    os.unlink(file_path)

        return env

    @staticmethod
    def get_srv_cnt(cartobj, host):
        print("Entering get_srv_cnt")
        """ get server count """
        hostlist = cartobj.params.get("{}".format(host), "/run/hosts/*/")

        srvcnt = 0

        srvcnt += len(hostlist)

        return srvcnt

    def build_cmd(self, cartobj, env, host):
        self.stdout.info("Entering build_cmd")
        """ build command """
        tst_cmd = ""

        tst_vgd = " valgrind --xml=yes " + \
                  "--xml-file={}/".format(cartobj.log_path) + \
                  r"valgrind.%q\{PMIX_ID\}.memcheck " + \
                  "--fair-sched=try  --partial-loads-ok=yes " + \
                  "--leak-check=yes --gen-suppressions=all " + \
                  "--suppressions=../etc/memcheck-cart.supp " + \
                  "--show-reachable=yes "


        self.init_mpi("openmpi")

        import os
        orterun_bin = find_executable("orterun", os.environ["PATH"] + ":/usr/lib64/openmpi3/bin")
        if orterun_bin is None:
            orterun_bin = "orterun_not_installed"

        self.print("\nbuild_cmd:227: ENV : %s\n" % os.environ)

        tst_bin = cartobj.params.get("{}_bin".format(host),
                                     "/run/tests/*/")
        tst_arg = cartobj.params.get("{}_arg".format(host),
                                     "/run/tests/*/")
        tst_env = cartobj.params.get("{}_env".format(host),
                                     "/run/tests/*/")
        tst_slt = cartobj.params.get("{}_slt".format(host),
                                     "/run/tests/*/")
        tst_ctx = cartobj.params.get("{}_CRT_CTX_NUM".format(host),
                                     "/run/defaultENV/")

        tst_host = cartobj.params.get("{}".format(host), "/run/hosts/*/")
        tst_ppn = cartobj.params.get("{}_ppn".format(host), "/run/tests/*/")
        logparse = cartobj.params.get("logparse", "/run/tests/*/")

        if tst_slt is not None:
            hostfile = self.write_host_file(tst_host, tst_slt)
        else:
            hostfile = self.write_host_file(tst_host, tst_ppn)

        mca_flags = "--mca btl self,tcp "

        if self.provider == "ofi+psm2":
            mca_flags += "--mca pml ob1 "

        tst_cmd = "{} {} -N {} --hostfile {} "\
                  .format(orterun_bin, mca_flags, tst_ppn, hostfile)

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

    def launch_srv_cli_test(self, cartobj, srvcmd, clicmd):
        self.stdout.info("Entering launch_srv_cli_test")
        """ launches sever in the background and client in the foreground """

        srv_rtn = self.launch_cmd_bg(cartobj, srvcmd)

        # Verify the server is still running.
        if not self.check_process(srv_rtn):
            procrtn = self.stop_process(srv_rtn, self)
            cartobj.fail("Server did not launch, return code %s" \
                       % procrtn)

        cli_rtn = self.launch_test(cartobj, clicmd, srv_rtn)

        srv_rtn = self.stop_process(srv_rtn, self)

        if srv_rtn:
            cartobj.fail("Failed, return codes client %d " % cli_rtn + \
                      "server %d" % srv_rtn)

        return 0

    def init_mpi_old(self, mpi):
        self.stdout.info("Entering init_mpi_old")
        """load mpi with older environment-modules"""
        self.print("Loading old %s" % mpi)
        self.module('purge')
        self.module('load', mpi)
        return True

    def init_mpi(self, mpi):
        self.stdout.info("Entering init_mpi")
        """load mpi"""

        mpich = ['mpi/mpich-x86_64']
        openmpi = ['mpi/openmpi3-x86_64', 'mpi/openmpi-x86_64']

        init_file = '/usr/share/Modules/init/python.py'

        if mpi == "mpich":
            load = mpich
            unload = openmpi
        else:
            load = openmpi
            unload = mpich

        #initialize Modules
        if not os.path.exists(init_file):
            if not self.module_init:
                self.print("Modules (environment-modules) is not installed")
            self.module_init = True
            return False

        #pylint: disable=exec-used
        #pylint: disable=undefined-variable
        if not self.module_init:
            exec(open(init_file).read())
            self.module = module
            self.module_init = True
        #pylint: enable=exec-used
        #pylint: enable=undefined-variable

        try:
            with open(os.devnull, 'w') as devnull:
                subprocess.check_call(['sh', '-l', '-c', 'module -V'], 
                  stdout=devnull,
                  stderr=devnull)
        except subprocess.CalledProcessError:
            # older version of module return -1
            return self.init_mpi_old(load[0])

        self.print("Checking for loaded modules")
        for to_load in load:
            if self.module('is-loaded', to_load):
                self.print("%s is already loaded" % to_load)
                return True

        for to_unload in unload:
            if self.module('is-loaded', to_unload):
                self.module('unload', to_unload)
                self.print("Unloading %s" % to_unload)

        for to_load in load:
            if self.module('load', to_load):
                self.print("Loaded %s" % to_load)
                return True

        self.print("No MPI found on system")
        return False

    def launch_test(self, cartobj, cmd, srv1=None, srv2=None):
        self.stdout.info("Entering launch_test")
        """ launches test """

        self.print("\nCMD : %s\n" % cmd)

        import os
        self.print("\nENV : %s\n" % os.environ)

        cmd = shlex.split(cmd)
        rtn = subprocess.call(cmd)

        self.print("\nENV : %s\n" % os.environ)

        if rtn:
            if srv1 is not None:
                self.stop_process(srv1, self)

            if srv2 is not None:
                self.stop_process(srv2, self)

            cartobj.fail("Failed, return codes %d " % rtn)

        return rtn

    def launch_cmd_bg(self, cartobj, cmd):
        self.stdout.info("Entering launch_cmd_bg")
        """ launches the given cmd in background """

        self.print("\nCMD : %s\n" % cmd)

        cmd = shlex.split(cmd)
        rtn = subprocess.Popen(cmd)

        if rtn is None:
            cartobj.fail("Failed to start command\n")
            return -1

        return rtn

    def print(self, cmd):
        self.stdout.info("Entering print")
        """ prints the given cmd at runtime and stdout """

        self.stdout.info(cmd)
        self.progress_log.info(cmd)

    @staticmethod
    def log_copy(cartobj):
        print("Entering log_copy")
        """Copy cart log files to Jenkins-accessible directory """

        import shutil
        import tempfile

        print("Copy log path", cartobj.log_path, " to ", os.environ["AVOCADO_TEST_LOGDIR"])
        # Copy log files to jenkins dir

        # Source path  
        src = cartobj.log_path

        # Destination path  
        _dest = os.environ["AVOCADO_TEST_LOGDIR"]
        dest = tempfile.mkdtemp(dir = _dest)

        # Copy the content of source to destination  
        rc = shutil.copytree(src, dest + "-testLogs")
        print("shutil.copytree: ", src, " to ", dest, ",  rc = ", rc)

    @staticmethod
    def log_check(cartobj):
        print("Entering log_check")
        """Check log files for consistency """

        logparse = cartobj.params.get("logparse", "/run/tests/*/")
        if logparse is None or not logparse:
            return

        strict_test = False
        print("Parsing log path", cartobj.log_path)
        if not os.path.exists(cartobj.log_path):
            print("Path does not exist")
            return

        for filename in os.listdir(cartobj.log_path):
            log_file = os.path.join(cartobj.log_path, filename)
            if not os.path.isfile(log_file):
                print("File is a Directory. Skipping.... :", log_file)
                continue

            print("Parsing ", log_file)
            cl = cart_logparse.LogIter(log_file)
            c_log_test = cart_logtest.LogTest(cl)
            c_log_test.check_log_file(strict_test)

    def dump_cart_logs(self):
        print("Entering dump_cart_logs")
        """Print contents of cart.log

        Returns:
            void
        """

        if self.cart_logs_dumped_already:
          print ("We already dumped the cart.log. No need to repeat.")
          return

        pattern = 'cart.log*'

        # Find the cart.log in one of these directories (which might differ between
        # different testing environments)
        directories = [
          '.',
          os.environ["AVOCADO_TEST_LOGDIR"],
          os.environ["AVOCADO_TEST_OUTPUTDIR"],
          os.environ["AVOCADO_TEST_WORKDIR"],
          os.environ["AVOCADO_TESTS_COMMON_TMPDIR"]
        ]

        for directory in directories:
          for root, dirs, files in os.walk(directory):
              for basename in files:
                  if fnmatch.fnmatch(basename, pattern):
                      filename = os.path.join(root, basename)
                      print('Found cart.log file: ', filename)
                      log = open(filename, "r").read()
                      print(log)
                      self.cart_logs_dumped_already = True

                      # Assume there's only one cart.log per test process
                      return

    @staticmethod
    def dump_cart_logs_static(self):
        """Static wrapper for dump_cart_logs

        Returns:
            void
        """
        self.dump_cart_logs()
