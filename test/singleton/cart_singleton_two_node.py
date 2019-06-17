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
import time
import tempfile
import os
import shutil

from avocado       import Test
from avocado       import main

sys.path.append('./util')

from cart_utils import CartUtils

class CartSingletonTwoNodeTest(Test):
    """
    Runs basic CaRT singleton tests

    :avocado: tags=all,singleton,two_node
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)
        self.tempdir = tempfile.mkdtemp()
        os.environ["CRT_PHY_ADDR_STR"] = self.params.get("CRT_PHY_ADDR_STR",
                                                         '/run/defaultENV/')
        os.environ["OFI_INTERFACE"] = self.params.get("OFI_INTERFACE",
                                                      '/run/defaultENV/')
        os.environ["CRT_CTX_NUM"] = self.params.get("cli_CRT_CTX_NUM",
                                                    '/run/defaultENV/')
        os.environ["CRT_CTX_SHARE_ADDR"] = self.params.get("CRT_CTX_SHARE_ADDR",
                                               '/run/env_CRT_CTX_SHARE_ADDR/*/')

    def tearDown(self):
        """ Test tear down """
        print("Run TearDown\n")
        shutil.rmtree(self.tempdir)

    def test_cart_singleton(self):
        """
        Test CaRT Singleton

        :avocado: tags=all,singleton,two_node
        """
        urifile = self.utils.create_uri_file()

        srvcmd = self.utils.build_cmd(self, self.env, "srv", True, urifile)

        srvcmd += " -p {} -s".format(self.tempdir)

        print("\nServer cmd : %s\n" % srvcmd)

        try:
            srv_rtn = self.utils.launch_cmd_bg(self, srvcmd)
        except Exception as e:
            print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        time.sleep(5)

        # Verify the server is still running.
        if not self.utils.check_process(srv_rtn):
            procrtn = self.utils.stop_process(srv_rtn)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)

        clicmd = self.params.get("cli_bin", '/run/tests/*/')

        clicmd += " -p {} -s".format(self.tempdir)

        print("\nClient cmd : %s\n" % clicmd)

        self.utils.launch_test(self, clicmd, srv_rtn)

        # Stop the server
        print("Stopping server process {}".format(srv_rtn))
        procrtn = self.utils.stop_process(srv_rtn)

        if procrtn:
            self.fail("Test failed. Server ret code {}".format(procrtn))

    def test_multi_tier_singleton_attach(self):
        """
        Test CaRT Multi_tier singleton attach test on two nodes

        :avocado: tags=all,singleton,multi_tier,two_node
        """
        urifile = self.utils.create_uri_file()

        srvcmd = self.utils.build_cmd(self, self.env, "srv", True, urifile)

        srvcmd += " -p {} -s -m".format(self.tempdir)

        srv2_bin = self.params.get("srv2_bin", '/run/tests/*/')
        srv2_ctx = self.params.get("srv2_CRT_CTX_NUM",
                                                        '/run/defaultENV/')

        srv2_hos = self.params.get("srv2", '/run/hosts/*/')
        srv2_ppn = self.params.get("srv2_ppn", '/run/tests/*/')
        hostfile = self.utils.write_host_file(srv2_hos, srv2_ppn)

        srvcmd += " : {} -x CRT_CTX_NUM={} -N {} --hostfile {} {}".format(self.env,
                                      srv2_ctx, srv2_ppn, hostfile, srv2_bin)

        print("\nServer cmd : %s\n" % srvcmd)

        try:
            srv_rtn = self.utils.launch_cmd_bg(self, srvcmd)
        except Exception as e:
            print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        time.sleep(10)

        # Verify the server is still running.
        if not self.utils.check_process(srv_rtn):
            procrtn = self.utils.stop_process(srv_rtn)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)

        clicmd = self.params.get("cli_bin", '/run/tests/*/')

        clicmd += " -p {} -s -m".format(self.tempdir)

        print("\nClient cmd : %s\n" % clicmd)

        self.utils.launch_test(self, clicmd, srv_rtn)

        # Stop the server
        print("Stopping server process {}".format(srv_rtn))
        procrtn = self.utils.stop_process(srv_rtn)

        if procrtn:
            self.fail("Test failed. Server ret code {}".format(procrtn))

    def test_multi_tier_without_singleton_attach(self):
        """
        Test CaRT Multi_tier without singleton attach test on two nodes

        :avocado: tags=all,singleton,multi_tier,two_node
        """
        urifile = self.utils.create_uri_file()

        srvcmd = self.utils.build_cmd(self, self.env, "srv", True, urifile)

        srvcmd += " -m"

        srv2cmd = self.utils.build_cmd(self, self.env, "srv2", False, urifile)


        print("\nServer cmd : %s\n" % srvcmd)

        try:
            srv_rtn = self.utils.launch_cmd_bg(self, srvcmd)
        except Exception as e:
            print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        time.sleep(10)

        print("\nServer cmd : %s\n" % srv2cmd)

        try:
            srv2_rtn = self.utils.launch_cmd_bg(self, srv2cmd)
        except Exception as e:
            print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        # Verify the server is still running.
        if not self.utils.check_process(srv_rtn):
            procrtn = self.utils.stop_process(srv_rtn)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)

        clicmd = self.utils.build_cmd(self, self.env, "cli", False, urifile)

        clicmd += " -m"

        print("\nClient cmd : %s\n" % clicmd)

        self.utils.launch_test(self, clicmd, srv_rtn, srv2_rtn)

        # Stop the server
        print("Stopping server process {}".format(srv_rtn))
        procrtn1 = self.utils.stop_process(srv_rtn)

        print("Stopping server process {}".format(srv2_rtn))
        procrtn2 = self.utils.stop_process(srv2_rtn)

        if procrtn1 or procrtn2:
            self.fail("Test failed. \
                       Server ret code {} {}".format(procrtn1, procrtn2))

if __name__ == "__main__":
    main()
