#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

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

from avocado import Test

sys.path.append('./util')

# Can't call this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartTest

class CartRpcOneNodeCrtEpAbortTest(CartTest):
    """
    Runs basic CaRT RPC tests

    :avocado: recursive
    """
    def test_cart_ep_abort(self):
        """
        Test CaRT RPC

        :avocado: tags=all,cart,pr,daily_regression,test_crt_ep_abort
        """
        srvcmd = self.build_cmd(self.env, "test_servers")
        clicmd = self.build_cmd(self.env, "test_clients")

        self.launch_srv_cli_test(srvcmd, clicmd)
        self.log_check()
