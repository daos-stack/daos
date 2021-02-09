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

import sys
import subprocess
import os

from apricot import TestWithoutServers

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartNoPmixOneNodeTest(TestWithoutServers):
    """
    Runs basic CaRT no_pmix tests

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        super(CartNoPmixOneNodeTest, self).setUp()

    def tearDown(self):
        """ Tear down """

        super(CartNoPmixOneNodeTest, self).tearDown()
        self.utils.cleanup_processes()

    def test_cart_no_pmix(self):
        """
        Test CaRT NoPmix

        :avocado: tags=all,cart,pr,daily_regression,no_pmix,one_node
        """

        crt_phy_addr   = os.environ["CRT_PHY_ADDR_STR"]
        ofi_interface  = os.environ["OFI_INTERFACE"]
        ofi_ctx_num    = os.environ["CRT_CTX_NUM"]
        ofi_share_addr = os.environ["CRT_CTX_SHARE_ADDR"]

        self.pass_env = {"CRT_PHY_ADDR_STR": crt_phy_addr,
                         "OFI_INTERFACE": ofi_interface,
                         "CRT_CTX_SHARE_ADDR": ofi_share_addr,
                         "CRT_CTX_NUM": ofi_ctx_num}

        cmd = self.params.get("tst_bin", '/run/tests/*/')

        self.utils.print("\nTest cmd : %s\n" % cmd)

        test_env = self.pass_env
        p = subprocess.Popen([cmd], env=test_env, stdout=subprocess.PIPE)

        rc = self.utils.wait_process(p, 10)
        if rc != 0:
            self.utils.print("Error waiting for process.")
            self.utils.print("returning {}".format(rc))
            self.fail("Test failed.\n")

        self.utils.print("Finished waiting for {}".format(p))
