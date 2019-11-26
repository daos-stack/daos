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

import os
import traceback

from apricot import TestWithServers

from pydaos.raw import DaosPool, DaosServer, DaosApiError


class PoolSvc(TestWithServers):
    """
    Tests svc argument while pool create.
    :avocado: recursive
    """

    def test_poolsvc(self):
        """
        Test svc arg during pool create.

        :avocado: tags=all,pool,pr,medium,svc
        """

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/createtests/createset/')
        createsize = self.params.get("size", '/run/createtests/createsize/')
        createsvc = self.params.get("svc", '/run/createtests/createsvc/*/')

        expected_result = createsvc[1]

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None, None, createsvc[0])
            self.pool.connect(1 << 1)

            # checking returned rank list for server more than 1
            iterator = 0
            while (
                    int(self.pool.svc.rl_ranks[iterator]) > 0 and
                    int(self.pool.svc.rl_ranks[iterator]) <= createsvc[0] and
                    int(self.pool.svc.rl_ranks[iterator]) != 999999
            ):
                iterator += 1
            if iterator != createsvc[0]:
                self.fail("Length of Returned Rank list is not equal to "
                          "the number of Pool Service members.\n")
            rank_list = []
            for iterator in range(createsvc[0]):
                rank_list.append(int(self.pool.svc.rl_ranks[iterator]))
                if len(rank_list) != len(set(rank_list)):
                    self.fail("Duplicate values in returned rank list")

            self.pool.pool_query()
            leader = self.pool.pool_info.pi_leader
            if createsvc[0] == 3:
                # kill pool leader and exclude it
                self.pool.pool_svc_stop()
                self.pool.exclude([leader])
                # perform pool disconnect, try connect again and disconnect
                self.pool.disconnect()
                self.pool.connect(1 << 1)
                self.pool.disconnect()
                # kill another server which is not a leader and exclude it
                server = DaosServer(self.context, self.server_group, 3)
                server.kill(1)
                self.pool.exclude([3])
                # perform pool connect
                self.pool.connect(1 << 1)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
