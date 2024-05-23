'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartMultisendOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run multi-send test that launches 16 servers and splits bulk
       transfers among them using different parameters.

    :avocado: recursive
    """

    def test_cart_multisend(self):
        """Test multi-send

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,rpc,one_node,memcheck,multisend,bulk
        :avocado: tags=CartMultisendOneNodeTest,test_cart_multisend
        """
        srvcmd = self.build_cmd(self.env, "test_servers")

        try:
            srv_rtn = self.launch_cmd_bg(srvcmd)
        # pylint: disable=broad-except
        except Exception as my_except:
            self.print("Exception in launching server : {}".format(my_except))
            self.fail("Test failed.\n")

        # Verify the server is still running.
        if not self.check_process(srv_rtn):
            procrtn = self.stop_process(srv_rtn)
            self.fail("Server did not launch, return code {}".format(procrtn))

        test_clients_arg = self.params.get("test_clients_arg", "/run/tests/*/")
        for index in range(len(test_clients_arg)):
            clicmd = self.build_cmd(self.env, "test_clients", index=index)
            self.launch_test(clicmd, srv_rtn)
        self.log_check()
