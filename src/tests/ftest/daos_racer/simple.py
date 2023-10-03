"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from daos_racer_utils import DaosRacerCommand


class DaosRacerSimpleTest(TestWithServers):
    """Test cases that utilize the daos_racer tool.

    :avocado: recursive
    """

    def test_daos_racer_simple(self):
        """JIRA-3855: daos_racer/consistency checker test.

        Test Description:
            The daos_racer test tool generates a bunch of simultaneous,
            conflicting I/O requests. After it is run it will verify that all
            the replicas of a given object are consistent.

            Run daos_racer for 5-10 minutes or so on 3-way replicated object
            data (at least 6 servers) and verify the object replicas.

        Use Cases:
            Running simultaneous, conflicting I/O requests.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=io,daos_racer
        :avocado: tags=DaosRacerSimpleTest,test_daos_racer_simple
        """
        dmg = self.get_dmg_command()
        self.assertGreater(
            len(self.hostlist_clients), 0,
            "This test requires one client: {}".format(self.hostlist_clients))
        daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0], dmg)
        daos_racer.get_params(self)
        daos_racer.run()
