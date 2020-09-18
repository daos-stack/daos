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

import sys
import time
import tempfile
import os
import shutil

from avocado       import Test
from avocado       import main

sys.path.append('./util')

from cart_utils import CartUtils

class CartGroupTiersThreeNodeTest(Test):
    """
    Runs basic CaRT group tier tests

    :avocado: tags=all,group_tiers,three_node
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Test tear down """
        print("Run TearDown\n")

    def test_basic_stuff(self):
        """
        Placeholder

        :avocado: tags=all,multi_tier,group_tiers,three_node
        """
        print("Nothing to run\n")

    def dont_run_test_group_tier(self):
        """
        Test CaRT group_tier

        #:avocado: tags=all,multi_tier,group_tiers,three_node
        """
        srvcmd = self.utils.build_cmd(self, self.env, "srv1")
        srv2cmd = self.utils.build_cmd(self, self.env, "srv2")

        try:
            srv2_rtn = self.utils.launch_cmd_bg(self, srv2cmd)
        except Exception as e:
            self.utils.print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        time.sleep(8)

        try:
            srv_rtn = self.utils.launch_cmd_bg(self, srvcmd)
        except Exception as e:
            self.utils.print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        time.sleep(4)

        clicmd = self.utils.build_cmd(self, self.env, "cli1")

        self.utils.launch_test(self, clicmd, srv_rtn, srv2_rtn)

        clicmd = self.utils.build_cmd(self, self.env, "cli2")

        self.utils.launch_test(self, clicmd, srv_rtn, srv2_rtn)

        # Stop the server
        self.utils.print("Stopping server process 2 {}".format(srv2_rtn))
        procrtn2 = self.utils.stop_process(srv2_rtn)

        self.utils.print("Stopping server process 1 {}".format(srv_rtn))
        procrtn1 = self.utils.stop_process(srv_rtn)

        if procrtn2 or procrtn1:
            self.fail("Test failed. \
                       server 1 ret code {} \
                       server 2 ret code {}".format(procrtn1, procrtn2))

if __name__ == "__main__":
    main()
