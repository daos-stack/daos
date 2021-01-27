#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys
import time

from apricot  import TestWithoutServers

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartCtlFiveNodeTest(TestWithoutServers):
    """
    Runs basic CaRT ctl tests

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Tear down """
        super(CartCtlFiveNodeTest, self).tearDown()
        self.utils.cleanup_processes()

    def test_cart_ctl(self):
        """
        Test CaRT ctl

        :avocado: tags=all,cart,pr,daily_regression,ctl,five_node
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

        time.sleep(5)

        for index in range(2):
            clicmd = self.utils.build_cmd(
                self, self.env, "test_clients", index=index)
            self.utils.launch_test(self, clicmd, srv_rtn)

        self.utils.stop_process(srv_rtn)


if __name__ == "__main__":
    main()
