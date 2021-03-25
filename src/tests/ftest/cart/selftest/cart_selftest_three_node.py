#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartSelfThreeNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Runs basic CaRT self test.

    :avocado: recursive
    """

    def test_cart_selftest(self):
        """Test CaRT Self Test.

        :avocado: tags=all,cart,pr,daily_regression,selftest,three_node
        """
        srvcmd = self.build_cmd(self.env, "test_servers")

        try:
            srv_rtn = self.launch_cmd_bg(srvcmd)
        # pylint: disable=broad-except
        except Exception as e:
            self.print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        # Verify the server is still running.
        if not self.check_process(srv_rtn):
            procrtn = self.stop_process(srv_rtn)
            self.fail("Server did not launch, return code {}".format(procrtn))

        for index in range(3):
            clicmd = self.build_cmd(self.env, "test_clients", index=index)
            self.launch_test(clicmd, srv_rtn)

        # Give few seconds for servers to fully shut down before exiting
        # from this test.
        if not self.wait_process(srv_rtn, 5):
            self.stop_process(srv_rtn)
