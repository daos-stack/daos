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

from avocado  import Test
from avocado  import main

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class GroupTest(Test):
    """
    Runs GroupTests for primary and secondary resizeable groups

    :avocado: tags=all,cart,pr,daily_regression,group_test,one_node,no_pmix
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def test_group(self):
        """
        Test CaRT NoPmix Launcher

        :avocado: tags=all,cart,pr,daily_regression,group_test,one_node
        """

        srv_cmd = self.utils.build_cmd(self, self.env, "test_servers")

        cmd = srv_cmd
        self.utils.launch_test(self, cmd)


if __name__ == "__main__":
    main()
