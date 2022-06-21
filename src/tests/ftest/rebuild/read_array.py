#!/usr/bin/python3
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from rebuild_test_base import RebuildTestBase
from daos_utils import DaosCommand

class RbldReadArrayTest(RebuildTestBase):
    # pylint: disable=too-many-ancestors
    """Run rebuild tests with DAOS servers and clients.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.daos_cmd = None

    def execute_during_rebuild(self):
        """Read the objects during rebuild."""

        self.daos_cmd = DaosCommand(self.bin)
        self.daos_cmd.container_set_prop(
                      pool=self.pool.uuid,
                      cont=self.container.uuid,
                      prop="status",
                      value="healthy")

        message = "Reading the array objects during rebuild"
        self.log.info(message)
        self.d_log.info(message)
        self.assertTrue(
            self.pool.read_data_during_rebuild(self.container),
            "Error reading data during rebuild")

    def test_read_array_during_rebuild(self):
        """Jira ID: DAOS-691.

        Test Description:
            Configure 5 targets with 1 pool with a service leader quantity
            of 2.  Add 1 container to the pool configured with 3 replicas.
            Add 10 objects of 10 records each populated with an array of 5
            values (currently a sufficient amount of data to be read fully
            before rebuild completes) to a specific rank.  Exclude this
            rank and verify that rebuild is initiated.  While rebuild is
            active, confirm that all the objects and records can be read.
            Finally verify that rebuild completes and the pool info indicates
            the correct number of rebuilt objects and records.

        Use Cases:
            Basic rebuild of container objects of array values with sufficient
            numbers of rebuild targets and no available rebuild targets.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild
        :avocado: tags=rebuildreadarray,test_read_array_during_rebuild
        """
        self.execute_rebuild_test()
