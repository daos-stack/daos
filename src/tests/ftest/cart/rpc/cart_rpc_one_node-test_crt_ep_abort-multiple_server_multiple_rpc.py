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

from avocado       import Test
from avocado       import main

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartRpcOneNodeCrtEpAbortTest(Test):
    """
    Runs basic CaRT RPC tests

    :avocado: tags=all,cart,pr,daily_regression,rpc,test_crt_ep_abort
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Test tear down """
        print("Run TearDown\n")

    def test_cart_rpc(self):
        """
        Test CaRT RPC

        :avocado: tags=all,cart,pr,daily_regression,test_crt_ep_abort
        """

        # Careful: lengths of test_ppn, test_servers, etc. must match
        #
        # We happen to have three tests here which vary on ppn and client_args, e.g.,
        #   -np 1 ... --issue_crt_ep_abort 0 --num_checkins_to_send 1
        #   -np 1 ... --issue_crt_ep_abort 0 --num_checkins_to_send 3
        #   -np 3 ... --issue_crt_ep_abort 2 --num_checkins_to_send 1
      
        num_tests = self.params.get("test_servers_ppn", '/run/tests/*/')
        for index in range(len(num_tests)):
          srvcmd = self.utils.build_cmd(self, self.env, "test_servers", index=index)

          print('DEBUG log: line 71, srvcmd  = ', srvcmd )

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

          clicmd = self.utils.build_cmd(
              self, self.env, "test_clients", index=index)
          self.utils.launch_test(self, clicmd, srv_rtn)

if __name__ == "__main__":
    main()
