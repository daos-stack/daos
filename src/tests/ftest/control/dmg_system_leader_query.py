#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.

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
  provided in Contract No. 8F-30005.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

from control_test_base import ControlTestBase

class DmgSystemLeaderQueryTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Verify the system leader-query function of the dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        super(DmgSystemLeaderQueryTest, self).__init__(*args, **kwargs)
        self.hosts = None

    def setUp(self):
        super(DmgSystemLeaderQueryTest, self).setUp()
        self.hosts = self.dmg.hostlist

    def test_dmg_system_leader_query(self):
        """
        JIRA ID: DAOS-4822
        Test Description: Test that system leader-query command reports leader
            consistently regardless of which server is queried.
        :avocado: tags=all,small,daily_regression,hw,dmg,system_leader_query
        :avocado: tags=basic
        """
        last_result = None
        original_hostlist = self.dmg.hostlist
        for host in self.hosts:
            print("Querying {0}\n".format(host))
            self.dmg.hostlist = [host]
            result = self.dmg.system_leader_query()

            status = result["status"]
            self.assertEqual(status, 0, "bad return status")

            leader = result["response"]["CurrentLeader"]
            if last_result:
                self.assertEqual(leader, last_result,
                                 "result for {0} didn't match previous".format(host))
            last_result = leader
        self.dmg.hostlist = original_hostlist
