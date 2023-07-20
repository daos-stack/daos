'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartNoPmixLauncherOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT no_pmix_launcher tests.

    :avocado: recursive
    """

    def test_cart_no_pmix_launcher(self):
        """Test CaRT NoPmix Launcher.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,no_pmix_launcher,one_node,memcheck
        :avocado: tags=CartNoPmixLauncherOneNodeTest,test_cart_no_pmix_launcher
        """
        cli_bin = self.params.get("test_clients_bin", '/run/tests/*/')
        cli_arg = self.params.get("test_clients_arg", '/run/tests/*/')
        cli_ppn = self.params.get("test_clients_ppn", '/run/tests/*/')

        srv_cmd = self.build_cmd(self.env, "test_servers")

        cmd = srv_cmd + " : -np {}".format(cli_ppn)
        cmd += self.get_env()
        cmd += " {}".format(cli_bin)
        cmd += " {}".format(cli_arg)

        self.launch_test(cmd)
