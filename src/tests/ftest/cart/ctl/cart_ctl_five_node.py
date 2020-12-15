#!/usr/bin/python
'''
  (C) Copyright 2018-2020 Intel Corporation.

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
import time

from avocado  import Test
from avocado  import main

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartCtlFiveNodeTest(Test):
    """
    Runs basic CaRT ctl tests

    :avocado: tags=all,cart,pr,ctl,five_node
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def test_cart_ctl(self):
        """
        Test CaRT ctl

        :avocado: tags=all,cart,pr,ctl,five_node
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
