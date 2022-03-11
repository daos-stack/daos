#!/usr/bin/python3
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading
import avocado
import time

from pydaos.raw import DaosApiError
from apricot import TestWithServers


def container_write(container, record, array_size=None):
    """Write to a container.

    Args:
        container: instance of TestContainer
        record: record size to be written
        array_size (optional): size of array value to be written
    """
    # update data_array_size to use array value type
    if array_size:
        container.data_array_size.update(array_size)
    # update record qty
    container.record_qty.update(record)
    print("\nRecord Size:{}".format(container.record_qty))

    # write multiple objects
    container.write_objects()


def container_read(container, array_size=None):
    """Read and verify the written data.

    Args:
        container: instance of TestContainer
        array_size (optional): size of array value to be written
    """
    # update data_array_size to use array value type
    if array_size:
        container.data_array_size.update(array_size)
    # read written objects and verify
    container.read_objects()


def test_runner(self, namespace, record_size, array_size, thread_per_size=4):
    """Perform simultaneous writes of varying record size to a container.

    Args:
        self (Test): avocado test object
        namespace (str): location from which to read the pool parameters
        record_size (list): list of different record sizes to be written
        array_size (optional): size of array value to be written
        thread_per_size (int): threads per rec size
    """
    # create a new pool
    self.pool.append(self.get_pool(namespace=namespace, connect=False))

    # display available space before write
    self.pool[-1].display_pool_daos_space("before writes")
    self.pool[-1].connect()

    # create a new container
    self.container.append(self.get_container(self.pool[-1]))
    self.container[-1].open()

    # initialize dicts to hold threads
    jobs = {"write": [], "read": []}

    # create read/write threads.
    for rec in record_size:
        for _ in range(thread_per_size):
            # create threads using single value type
            jobs["write"].append(
                threading.Thread(
                    target=container_write, args=(self.container[-1], rec)))
            jobs["read"].append(
                threading.Thread(
                    target=container_read, args=(self.container[-1], None)))

            # create threads using array value type
            jobs["write"].append(
                threading.Thread(
                    target=container_write,
                    args=(self.container[-1], rec, array_size)))
            jobs["read"].append(
                threading.Thread(
                    target=container_read,
                    args=(self.container[-1], array_size)))

    # start all the write threads
    for job in jobs["write"]:
        job.start()

    # wait for all write threads to finish
    for job in jobs["write"]:
        job.join()

    # start read threads
    for job in jobs["read"]:
        job.start()

    # wait for all read threads to complete
    for job in jobs["read"]:
        job.join()

    # display free space after reads and writes
    self.pool[-1].display_pool_daos_space("after writes and reads")

    # container and pool destroy handled by cleanup

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

        # initialize self.pool and self.container as lists
        self.pool = []
        self.container = []
        # set common params
        self.record_size = self.params.get("record_size", "/run/container/*")
        self.array_size = self.params.get("array_size", "/run/container/*")

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
        :avocado: tags=hw,large
        :avocado: tags=nvme_object,nvme_object_single_pool
        :avocado: tags=DAOS_5610
        """
        # perform multiple object writes to a single pool
        test_runner(self, "/run/pool_1/*", self.record_size[:-1], 0,
                    self.array_size)

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
        :avocado: tags=hw,large
        :avocado: tags=nvme_object,nvme_object_multiple_pools
        """
        # thread to perform simultaneous object writes to multiple pools
        threads = []
        for index in range(2):
            time.sleep(1)
            thread = threading.Thread(
                target=test_runner,
                args=(self, f"/run/pool_{index + 2}/*", self.record_size,
                      self.array_size))
            threads.append(thread)

        # starting all the threads
        for job in threads:
            job.start()

        # waiting for all threads to finish
        for job in threads:
            job.join()

        # run the test_runner after cleaning up all the pools for
        # very large nvme_pool size
        # Uncomment the below line after DAOS-3339 is resolved

        # test_runner(
        #     self, "/run/pool/pool_3", self.record_size, self.array_size)
