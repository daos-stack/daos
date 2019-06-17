#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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

import time
import os
import random
import json
import shlex
import subprocess

class CartUtils():
    """CartUtils Class"""

    def write_host_file(self, hostlist, slots=1):
        """ write out a hostfile suitable for orterun """

        unique = random.randint(1, 100000)

        path = "./hostfile"

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

    def create_uri_file(self):
        """ create uri file suitable for orterun """

        path = "./uri"
        unique = random.randint(1, 100000)

        if not os.path.exists(path):
            os.makedirs(path)
        urifile = path + "/urifile" + str(unique)

        return urifile

    def check_process(self, proc):
        """ check if a process is still running"""
        proc.poll()
        procrtn = proc.returncode
        if procrtn is None:
            return True
        return False

    def wait_process(self, proc, wait_time):
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

    def stop_process(self, proc):
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

        return procrtn

    def get_env(self, cartobj):
        """ return basic env setting in yaml """
        env_CCSA = cartobj.params.get("env", "/run/env_CRT_CTX_SHARE_ADDR/*/")
        test_name = cartobj.params.get("name", "/run/tests/*/")
        host_cfg = cartobj.params.get("config", "/run/hosts/*/")

        if env_CCSA is not None:
            log_dir = "{}-{}-{}-{}".format(test_name, host_cfg, cartobj.id(),
                                           env_CCSA)
        else:
            log_dir = "{}-{}-{}".format(test_name, host_cfg, cartobj.id())

        log_path = os.path.join("testLogs", log_dir)
        log_file = os.path.join(log_path, "output.log")

        log_mask = cartobj.params.get("D_LOG_MASK", "/run/defaultENV/")
        crt_phy_addr = cartobj.params.get("CRT_PHY_ADDR_STR",
                                          "/run/defaultENV/")
        ofi_interface = cartobj.params.get("OFI_INTERFACE", "/run/defaultENV/")
        ofi_share_addr = cartobj.params.get("CRT_CTX_SHARE_ADDR",
                                            "/run/env_CRT_CTX_SHARE_ADDR/*/")

        env = " --output-filename {!s}".format(log_path)

        env += " -x D_LOG_FILE={!s}".format(log_file)

        if log_mask is not None:
            env += " -x D_LOG_MASK={!s}".format(log_mask)

        if crt_phy_addr is not None:
            env += " -x CRT_PHY_ADDR_STR={!s}".format(crt_phy_addr)

        if ofi_interface is not None:
            env += " -x OFI_INTERFACE={!s}".format(ofi_interface)

        if ofi_share_addr is not None:
            env += " -x CRT_CTX_SHARE_ADDR={!s}".format(ofi_share_addr)

        cartobj.log_path = log_path

        if not os.path.exists(log_path):
            os.makedirs(log_path)

        return env

    def get_srv_cnt(self, cartobj, host):
        """ get server count """
        hostlist = cartobj.params.get("{}".format(host), "/run/hosts/*/")

        srvcnt = 0

        for host in hostlist:
            srvcnt += 1

        return srvcnt

    def build_cmd(self, cartobj, env, host, report_uri=True, urifile=None):
        """ build command """
        tst_cmd = ""

        tst_vgd = " valgrind --xml=yes " + \
                  "--xml-file={}/".format(cartobj.log_path) + \
                                "valgrind.%q\{PMIX_ID\}.memcheck " + \
                  "--fair-sched=try  --partial-loads-ok=yes " + \
                  "--leak-check=yes --gen-suppressions=all " + \
                  "--suppressions=../etc/memcheck-cart.supp " + \
                  "--show-reachable=yes "

        # get paths from the build_vars generated by build
        with open('.build_vars.json') as build_file:
            build_paths = json.load(build_file)

        orterun_bin = os.path.join(build_paths["OMPI_PREFIX"], "bin", "orterun")

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

        if tst_slt is not None:
            hostfile = self.write_host_file(tst_host,tst_slt)
        else:
            hostfile = self.write_host_file(tst_host,tst_ppn)

        tst_cmd = "{} --mca btl self,tcp -N {} --hostfile {} "\
                  .format(orterun_bin, tst_ppn, hostfile)

        if urifile is not None:
            if report_uri == True:
                tst_cmd += "--report-uri {} ".format(urifile)
            else:
                tst_cmd += "--ompi-server file:{} ".format(urifile)

        tst_cmd += env

        if tst_ctx is not None:
            tst_cmd += " -x CRT_CTX_NUM=" + tst_ctx

        if tst_env is not None:
            tst_cmd += " " + tst_env

        tst_mod = os.getenv("CART_TEST_MODE", "native")
        if tst_mod == "memcheck":
            tst_cmd += tst_vgd

        if tst_bin is not None:
            tst_cmd += " " + tst_bin

        if tst_arg is not None:
            tst_cmd += " " + tst_arg

        return tst_cmd

    def launch_srv_cli_test(self, cartobj, srvcmd, clicmd):
        """ launches sever in the background and client in the foreground """

        srv_rtn = self.launch_cmd_bg(cartobj, srvcmd)

        # Verify the server is still running.
        if not self.check_process(srv_rtn):
            procrtn = self.stop_process(srv_rtn)
            cartobj.fail("Server did not launch, return code %s" \
                       % procrtn)

        self.launch_test(cartobj, clicmd, srv_rtn)

        srv_rtn = self.stop_process(srv_rtn)

        if srv_rtn:
            cartobj.fail("Failed, return codes client %d " % cli_rtn + \
                      "server %d" % srv_rtn)

        return 0

    def launch_test(self, cartobj, cmd, srv1=None, srv2=None):
        """ launches test """

        cmd = shlex.split(cmd)
        rtn = subprocess.call(cmd)

        if rtn:
            if srv1 is not None:
                self.stop_process(srv1)

            if srv2 is not None:
                self.stop_process(srv2)

            cartobj.fail("Failed, return codes %d " % rtn)

        return rtn

    def launch_cmd_bg(self, cartobj, cmd):
        """ launches the given cmd in background """

        cmd = shlex.split(cmd)
        rtn = subprocess.Popen(cmd)

        if rtn is None:
            return -1

        return rtn
