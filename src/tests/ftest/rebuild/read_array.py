"""
  (C) Copyright 2019-2022 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from general_utils import DaosTestError
from rebuild_test_base import RebuildTestBase


class RbldReadArrayTest(RebuildTestBase):
    """Run rebuild tests with DAOS servers and clients.

    :avocado: recursive
    """

    def execute_during_rebuild(self, container):
        """Read the objects during rebuild.

        Args:
            container (TestContainer): container to read from
        """
        container.set_prop(prop="status", value="healthy")
        container.open()

        self.log.info(
            "Reading objects in container %s/%s during rebuild",
            container.pool.identifier, container.identifier)

        # Attempt to read all of the data from the container during rebuild
        index = 0
        success = read_incomplete = index < len(container.written_data)
        while not container.pool.has_rebuild_completed() and read_incomplete:
            try:
                success &= container.written_data[index].read_object(container)
            except DaosTestError as error:
                self.log.error(str(error))
                success = False
            index += 1
            read_incomplete = index < len(container.written_data)

        # Verify that all of the container data was read successfully
        if not success:
            self.fail("Error reading data during rebuild")

        if read_incomplete:
            self.log.info(
                "Rebuild completed before all the written data could be read - "
                "Currently not reporting this as an error.")
            # success = False

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
        :avocado: tags=rebuild,read_array
        :avocado: tags=RbldReadArrayTest,test_read_array_during_rebuild
        """
        self.execute_rebuild_test()
