"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import avocado

from pydaos.raw import DaosApiError

from apricot import TestWithServers
from thread_manager import ThreadManager
from general_utils import report_errors


class NvmeObject(TestWithServers):
    """Test class for NVMe storage.

     Creates/Updates/Fetches large number of objects simultaneously.

    Test Class Description:
        Test the general functional operations of objects on nvme storage
        i.e. Creation/Updating/Fetching for single pool and multiple pools.

    :avocado: recursive
    """

    def setUp(self):
        """Set Up nodes for each test case."""
        super().setUp()

        # to track errors from threads
        self.errors = []
        # set common params
        self.record_size = self.params.get("record_size", "/run/container/*")
        self.array_size = self.params.get("array_size", "/run/container/*")

    def container_write(self, container, record, array_size=None):
        """Write to a container.

        Args:
            container (TestContainer): container to write
            record (int): record size to be written
            array_size (int, optional): size of array value to be written
        """
        # update data_array_size to use array value type
        if array_size:
            container.data_array_size.update(array_size)
        # update record qty
        container.record_qty.update(record)
        self.log.info("Record Size: %s", container.record_qty)

        # write multiple objects
        container.write_objects()

    @staticmethod
    def container_read(container, array_size=None):
        """Read and verify the written data.

        Args:
            container (TestContainer): container to read
            array_size (int, optional): size of array value to be written
        """
        # update data_array_size to use array value type
        if array_size:
            container.data_array_size.update(array_size)
        # read written objects and verify
        container.read_objects()

    def test_runner(self, namespace, record_size, array_size, thread_per_size=4):
        """Perform simultaneous writes of varying record size to a container.

        Args:
            namespace (str): location from which to read the pool parameters
            record_size (list): list of different record sizes to be written
            array_size (int): size of array value to be written
            thread_per_size (int, optional): threads per rec size
        """
        # create a new pool
        pool = self.get_pool(namespace=namespace, connect=False, dmg=self.get_dmg_command().copy())

        # display available space before write
        self.log.info("%s space before writes", str(pool))
        pool.display_space()
        pool.connect()

        # create a new container
        container = self.get_container(pool)
        container.open()

        # initialize thread managers
        write_manager = ThreadManager(self.container_write, self.get_remaining_time() - 30)
        read_manager = ThreadManager(self.container_read, self.get_remaining_time() - 30)

        # create read/write threads.
        for rec in record_size:
            for _ in range(thread_per_size):
                # create threads using single value type
                write_manager.add(container=container, record=rec, array_size=array_size)
                read_manager.add(container=container, array_size=None)

                # create threads using array value type
                write_manager.add(container=container, record=rec, array_size=None)
                read_manager.add(container=container, array_size=array_size)

        # Run the write threads and check for errors
        write_result = write_manager.run()
        for result in write_result:
            if not result.passed:
                self.errors.append(result.result)
        if self.errors:
            return

        # Run the read threads and check for errors
        read_result = read_manager.run()
        for result in read_result:
            if not result.passed:
                self.errors.append(result.result)
        if self.errors:
            return

        # display free space after reads and writes
        self.log.info("%s space after writes and reads", str(pool))
        pool.display_space()

        # Cleanup the container and pool
        container.destroy()
        pool.destroy()

    @avocado.fail_on(DaosApiError)
    def test_nvme_object_single_pool(self):
        """Jira ID: DAOS-2087.

        Test Description:
            Test will create single pool on nvme using TestPool
            Create large number of objects
            Update/Fetch with different object ID in single pool

        Use Cases:
            Verify the objects are being created and the data is not
            corrupted.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme
        :avocado: tags=NvmeObject,test_nvme_object_single_pool
        """
        # perform multiple object writes to a single pool
        self.test_runner("/run/pool_1/*", self.record_size[:-1], 0, self.array_size)
        report_errors(self, self.errors)

    @avocado.fail_on(DaosApiError)
    def test_nvme_object_multiple_pools(self):
        """Jira ID: DAOS-2087.

        Test Description:
            Test will create multiple pools on nvme using TestPool
            Create large number of objects for each pool
            Update/Fetch with different object ID in multiple pools

        Use Cases:
            Verify the objects are being created and the data is not
            corrupted.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme
        :avocado: tags=NvmeObject,test_nvme_object_multiple_pools
        """
        # thread to perform simultaneous object writes to multiple pools
        runner_manager = ThreadManager(self.test_runner, self.get_remaining_time() - 30)
        runner_manager.add(
            namespace='/run/pool_1/*', record_size=self.record_size, array_size=self.array_size)
        runner_manager.add(
            namespace='/run/pool_2/*', record_size=self.record_size, array_size=self.array_size)

        # Run the write threads and check for errors
        runner_result = runner_manager.run()
        for result in runner_result:
            if not result.passed:
                self.errors.append(result.result)
        report_errors(self, self.errors)

        # run the test_runner after cleaning up all the pools for large nvme_pool size
        self.test_runner("/run/pool_3/*", self.record_size, self.array_size)
        report_errors(self, self.errors)
