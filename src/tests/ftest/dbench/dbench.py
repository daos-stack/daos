#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ClusterShell.NodeSet import NodeSet
from dfuse_test_base import DfuseTestBase
from exception_utils import CommandFailure
from dbench_utils import Dbench

class DbenchTest(DfuseTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
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
        :avocado: tags=hw,medium,ib2
        :avocado: tags=dbench,dfuse
        """

        self.add_pool(connect=False)
        self.add_container(self.pool)
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        dbench_cmd = Dbench(self.hostlist_clients, self.tmp)
        dbench_cmd.get_params(self)
        dbench_cmd.directory.update(self.dfuse.mount_dir.value)

        try:
            # Start dfuse
            dbench_cmd.run()
        except CommandFailure as error:
            self.log.error(
                "Dbench command %s failed on hosts %s", str(dbench_cmd),
                str(NodeSet.fromlist(dbench_cmd.hosts)), exc_info=error)
            self.fail("Test was expected to pass but it failed.")

        # stop dfuse
        self.stop_dfuse()
        # destroy container
        self.container.destroy()
        # destroy pool
        self.pool.destroy()
