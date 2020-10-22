#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
"""
from __future__ import print_function

import threading
import avocado

from pydaos.raw import DaosApiError
from test_utils_pool import TestPool
from test_utils_container import TestContainer
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


def test_runner(self, size, record_size, index, array_size, thread_per_size=4):
    """Perform simultaneous writes of varying record size to a container.

    Args:
        self: avocado test object
        size: pool size to be created
        record_size (list): list of different record sizes to be written
        index (int): pool/container object index
        array_size (optional): size of array value to be written
        thread_per_size (int): threads per rec size
    """
    # pool initialization
    self.pool.append(TestPool(
        self.context, dmg_command=self.get_dmg_command()))
    self.pool[index].get_params(self)

    # set pool size
    self.pool[index].nvme_size.update(size)

    # Create a pool
    self.pool[index].create()

    # display available space before write
    self.pool[index].display_pool_daos_space("before writes")
    self.pool[index].connect()

    # create container
    self.container.append(TestContainer(self.pool[index]))
    self.container[index].get_params(self)
    self.container[index].create()
    self.container[index].open()

    # initialize dicts to hold threads
    jobs = {"write": [], "read": []}

    # create read/write threads.
    for rec in record_size:
        for _ in range(thread_per_size):
            # create threads using single value type
            jobs["write"].append(threading.Thread(target=container_write,
                                                  args=(self.container[index],
                                                        rec)))
            jobs["read"].append(threading.Thread(target=container_read,
                                                 args=(self.container[index],
                                                       None)))

            # create threads using array value type
            jobs["write"].append(threading.Thread(target=container_write,
                                                  args=(self.container[index],
                                                        rec, array_size)))
            jobs["read"].append(threading.Thread(target=container_read,
                                                 args=(self.container[index],
                                                       array_size)))

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
    self.pool[index].display_pool_daos_space("after writes and reads")

    # destroy container
    if self.container[index] is not None:
        self.container[index].destroy()

    # destroy pool
    if self.pool[index] is not None:
        self.pool[index].destroy(1)


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
        super(NvmeObject, self).setUp()

        # initialize self.pool and self.container as lists
        self.pool = []
        self.container = []
        # set common params
        self.record_size = self.params.get("record_size", "/run/container/*")
        self.pool_size = self.params.get("size", "/run/pool/createsize/*")
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

        :avocado: tags=all,pr,hw,large,nvme_object_single_pool,nvme_object
        :avocado: tags=DAOS_5610
        """
        # perform multiple object writes to a single pool
        test_runner(self, self.pool_size[0], self.record_size[:-1], 0,
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

        :avocado: tags=all,full_regression,hw,large,nvme_object_multiple_pools
        :avocado: tags=nvme_object
        """
        # thread to perform simulatneous object writes to multiple pools
        threads = []
        index = 0
        for size in self.pool_size[:-1]:
            thread = threading.Thread(target=test_runner,
                                      args=(self, size, self.record_size,
                                            index, self.array_size))
            threads.append(thread)
            index += 1

        # starting all the threads
        for job in threads:
            job.start()

        # waiting for all threads to finish
        for job in threads:
            job.join()

        # run the test_runner after cleaning up all the pools for
        # very large nvme_pool size
        # Uncomment the below line after DAOS-3339 is resolved

        # test_runner(self, self.pool_size[2], self.record_size, index,
        #            self.array_size)
