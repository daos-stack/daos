#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys

from apricot  import TestWithoutServers

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartSelfThreeNodeTest(TestWithoutServers):
    """
    Runs basic CaRT self test

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Tear down """
        super(CartSelfThreeNodeTest, self).tearDown()
        self.utils.cleanup_processes()

    def test_cart_selftest(self):
        """
        Test CaRT Self Test

        :avocado: tags=all,cart,pr,daily_regression,selftest,three_node
        """

        srvcmd = self.utils.build_cmd(self, self.env, "test_servers")

        try:
            srv_rtn = self.utils.launch_cmd_bg(self, srvcmd)
        # pylint: disable=broad-except
        except Exception as e:
            self.utils.print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        # Verify the server is still running.
        if not self.utils.check_process(srv_rtn):
            procrtn = self.utils.stop_process(srv_rtn)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)

        for index in range(3):
            clicmd = self.utils.build_cmd(
                self, self.env, "test_clients", index=index)
            self.utils.launch_test(self, clicmd, srv_rtn)

if __name__ == "__main__":
    main()
