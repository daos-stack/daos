#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartRpcOneNodeSwimNotificationOnRankEvictionTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT RPC tests.

    :avocado: recursive
    """

    def test_cart_rpc(self):
        """Test CaRT RPC.

        :avocado: tags=all,cart,pr,rpc,one_node,swim_rank_eviction
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

        for index in range(6):
            clicmd = self.build_cmd(self.env, "test_clients", index=index)
            self.launch_test(clicmd, srv_rtn)
