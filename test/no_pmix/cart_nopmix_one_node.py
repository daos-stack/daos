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

class CartNoPmixOneNodeTest(Test):
    """
    Runs basic CaRT no_pmix tests on one-node

    :avocado: tags=all,no_pmix,one_node
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)
        crt_phy_addr = self.params.get("CRT_PHY_ADDR_STR", '/run/defaultENV/')
        ofi_interface = self.params.get("OFI_INTERFACE", '/run/defaultENV/')
        ofi_ctx_num = self.params.get("CRT_CTX_NUM", '/run/defaultENV/')
        ofi_share_addr = self.params.get("CRT_CTX_SHARE_ADDR",
                                               '/run/defaultENV/')

        self.pass_env = {"CRT_PHY_ADDR_STR": crt_phy_addr,
                         "OFI_INTERFACE": ofi_interface,
                         "CRT_CTX_SHARE_ADDR": ofi_share_addr,
                         "CRT_CTX_NUM": ofi_ctx_num}

    def tearDown(self):
        """ Test tear down """
        print("Run TearDown\n")

    def test_cart_no_pmix(self):
        """
        Test CaRT NoPmix

        :avocado: tags=all,no_pmix,one_node
        """

        test_bin = self.params.get("tst_bin", '/run/tests/*/')

        self.utils.print("\nTest cmd : %s\n" % test_bin)

        ranks = [1, 2, 3, 10, 4]
        master_rank = 10

        for x in ranks:
            tmp_file = "/tmp/no_pmix_rank{}.uri_info".format(x)
            if os.path.exists(tmp_file):
                os.remove(tmp_file)

        process_other = []
        arg_master = ",".join(map(str, ranks))

        test_env = self.pass_env
        p1 = subprocess.Popen([test_bin, '{}'.format(master_rank),
                               '-m {}'.format(arg_master)], env=test_env,
                              stdout=subprocess.PIPE)

        for rank in ranks:
            if rank is master_rank:
                continue

            p = subprocess.Popen([test_bin, '{}'.format(rank)],
                                 env=test_env, stdout=subprocess.PIPE)
            process_other.append(p)


        for x in process_other:
            rc = self.utils.wait_process(x, 10)

            if rc != 0:
                self.utils.print("Error waiting for process. returning {}".format(rc))
                return rc

            self.utils.print("Finished waiting for {}".format(x))

        rc = self.utils.wait_process(p1, 10)
        if rc != 0:
            self.utils.print("error waiting for master process {}".format(rc))
            return rc

        self.utils.print("everything finished successfully")

if __name__ == "__main__":
    main()
