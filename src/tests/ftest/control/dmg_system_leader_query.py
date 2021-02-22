#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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
        :avocado: tags=all,basic,daily_regression,dmg,system_leader_query
        """
        last_result = None
        for host in self.hosts:
            print("Querying {0}\n".format(host))
            self.dmg.hostlist = [host]
            result = self.dmg.system_leader_query()

            status = result["status"]
            self.assertEqual(status, 0, "bad return status")

            leader = result["response"]["CurrentLeader"]
            if last_result:
                self.assertEqual(leader, last_result,
                                 ("current leader for host {0} didn't match "
                                  "previous").format(host))
            last_result = leader
        self.dmg.hostlist = self.hosts
