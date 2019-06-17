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

import os
import sys
import subprocess

from avocado       import Test
from avocado       import main

sys.path.append('./util')

from cart_utils import CartUtils

class CartNoPmixLauncherOneNodeTest(Test):
    """
    Runs basic CaRT no_pmix_launcher tests

    :avocado: tags=all,no_pmix_launcher,one_node
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Test tear down """
        print("Run TearDown\n")

    def test_cart_no_pmix_launcher(self):
        """
        Test CaRT NoPmix Launcher

        :avocado: tags=all,no_pmix_launcher,one_node
        """

        cli_bin = self.params.get("cli_bin", '/run/tests/*/')
        cli_arg = self.params.get("cli_arg", '/run/tests/*/')
        cli_ppn = self.params.get("cli_ppn", '/run/tests/*/')
        log_mask = self.params.get("D_LOG_MASK", "/run/defaultENV/")
        crt_phy_addr = self.params.get("CRT_PHY_ADDR_STR",
                                          "/run/defaultENV/")
        ofi_interface = self.params.get("OFI_INTERFACE", "/run/defaultENV/")

        srv_cmd = self.utils.build_cmd(self, self.env, "srv")

        cmd = srv_cmd + " : -np {}".format(cli_ppn)
        cmd += " -x CRT_PHY_ADDR_STR={}".format(crt_phy_addr)
        cmd += " -x OFI_INTERFACE={}".format(ofi_interface)
        cmd += " -x D_LOG_MASK={}".format(log_mask)
        cmd += " {}".format(cli_bin)
        cmd += " {}".format(cli_arg)

        print("\nTest cmd : %s\n" % cmd)
        self.utils.launch_test(self, cmd)

if __name__ == "__main__":
    main()
