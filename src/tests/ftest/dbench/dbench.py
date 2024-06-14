"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from dbench_utils import Dbench
from dfuse_utils import get_dfuse, start_dfuse
from exception_utils import CommandFailure


class DbenchTest(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Base Dbench test class.

    :avocado: recursive
    """

    def test_dbench(self):
        """Jira ID: DAOS-4780

        Test Description:
            Purpose of this test is to mount dfuse and run
            dbench on top of it.

        Use cases:
            Create Pool.
            Create container.
            Mount dfuse.
            Run dbench on top of mount point.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dbench,dfuse
        :avocado: tags=DbenchTest,test_dbench
        """

        self.log_step('Creating a single pool and container')
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        self.log_step('Starting dfuse')
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        self.log_step('Running dbench')
        dbench_cmd = Dbench(self.hostlist_clients, self.tmp)
        dbench_cmd.get_params(self)
        dbench_cmd.directory.update(dfuse.mount_dir.value)
        try:
            # Start dfuse
            dbench_cmd.run()
        except CommandFailure as error:
            self.log.error(
                "Dbench command %s failed on hosts %s", str(dbench_cmd),
                str(NodeSet.fromlist(dbench_cmd.hosts)), exc_info=error)
            self.fail("Test was expected to pass but it failed.")

        self.log.info('Test passed')
