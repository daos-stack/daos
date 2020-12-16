#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys

from avocado  import Test
from avocado  import main

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class GroupTest(Test):
    """
    Runs GroupTests for primary and secondary resizeable groups

    :avocado: tags=all,cart,pr,group_test,one_node,no_pmix
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def test_group(self):
        """
        Test CaRT NoPmix Launcher

        :avocado: tags=all,cart,pr,group_test,one_node
        """

        srv_cmd = self.utils.build_cmd(self, self.env, "test_servers")

        cmd = srv_cmd
        self.utils.launch_test(self, cmd)


if __name__ == "__main__":
    main()
