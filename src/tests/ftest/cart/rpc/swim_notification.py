'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os

from cart_utils import CartTest


class CartRpcOneNodeSwimNotificationOnRankEvictionTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT RPC tests.

    :avocado: recursive
    """

    def test_cart_rpc(self):
        """Test CaRT RPC.

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=cart,rpc,one_node,swim_rank_eviction,memcheck
        :avocado: tags=CartRpcOneNodeSwimNotificationOnRankEvictionTest,test_cart_rpc
        """
        srvcmd = self.build_cmd(self.env, "test_servers")

        try:
            srv_rtn = self.launch_cmd_bg(srvcmd)
        # pylint: disable=broad-except
        except Exception as error:
            self.print("Exception in launching server : {}".format(error))
            self.fail("Test failed.\n")

        # Verify the server is still running.
        if not self.check_process(srv_rtn):
            procrtn = self.stop_process(srv_rtn)
            self.fail("Server did not launch, return code {}".format(procrtn))

        test_clients_arg = self.params.get("test_clients_arg", "/run/tests/*/")
        for index in range(len(test_clients_arg)):
            clicmd = self.build_cmd(self.env, "test_clients", index=index)
            self.launch_test(clicmd, srv_rtn)

        daos_test_shared_dir = os.getenv("DAOS_TEST_SHARED_DIR",
                                         os.getenv("HOME"))

        # Each of the three servers should leave a completion file (with their
        # pid appended)
        glob_pat = daos_test_shared_dir + "/test-servers-completed.txt.*"

        # Verify the server(s) exited gracefully
        if not self.check_files(glob_pat, count=3, retries=4):
            self.print("Didn't find completion file(s): '{}'. "
                       "This indicates not all CaRT binaries exited "
                       "gracefully. "
                       "Marking test pass while DAOS-7892 remains "
                       "unresolved.\n".format(glob_pat))
