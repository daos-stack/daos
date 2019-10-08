#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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

from daos_api import DaosApiError
from test_utils import TestPool, TestContainer
from apricot import TestWithServers


def container_write(container, record):
    """Method to write to a container

       Args:
         container: instance of TestContainer
         record: record size to be written
    """
    # update record qty
    container.record_qty.update(record)
    print("\nRecord Size:{}".format(container.record_qty))

    # write multiple objects
    container.write_objects()

def container_read(container):
    """Method to read and verify the written data

       Args:
         container: instance of TestContainer
    """

    # read written objects and verify
    container.read_objects()

def test_runner(self, size, record_size):
    """Method to perform simultaneous writes of varying
       record size to a container in a pool

       Args:
         self: avocado test object
         size: pool size to be created
         record_size (list): list of different record sizes to be written
    """
    # pool initialization
    pool = TestPool(self.context, self.log)
    pool.get_params(self)
    # set pool size
    pool.nvme_size.update(size)
    # Create a pool
    pool.create()
    pool.connect()

    # invoke pool query before write
    pool.get_info()
    for index in range(2):
        self.log.info("Pool Total Space [%d]: %s", index,
                      (getattr(pool.info.pi_space.ps_space, 's_total')[index]))
        self.log.info("Pool Free Space [%d]: %s", index,
                      (getattr(pool.info.pi_space.ps_space, 's_free')[index]))

    # create container
    container = TestContainer(pool)
    container.get_params(self)
    container.create()
    container.open()

    threads_write = []
    threads_read = []
    for rec in record_size:
        for thread_write in range(16):
            thread_write = threading.Thread(target=container_write,
                                            args=(container, rec))
            thread_read = threading.Thread(target=container_read,
                                           args=(container,))
            threads_write.append(thread_write)
            threads_read.append(thread_read)

    # start all the write threads
    for job in threads_write:
        job.start()

    # wait for all write threads to finish
    for job in threads_write:
        job.join()

    # start read threads
    for job in threads_read:
        job.start()

    # wait for all read threads to complete
    for job in threads_read:
        job.join()

    # invoke pool query after write
    for idx in range(2):
        self.log.info("Pool Total Space [%d]: %s", idx,
                      (getattr(pool.info.pi_space.ps_space, 's_total')[idx]))
        self.log.info("Pool Free Space [%d]: %s", idx,
                      (getattr(pool.info.pi_space.ps_space, 's_free')[idx]))

    # destroy container
    if container is not None:
        container.destroy()

    # destroy pool
    if pool is not None:
        pool.destroy(1)

class NvmeObject(TestWithServers):
    """Test class for NVMe storage by creating/Updating/Fetching
       large number of objects simultaneously.

    Test Class Description:
        Test the general functional operations of objects on nvme storage
        i.e. Creation/Updating/Fetching for single pool and multiple pools.

    :avocado: recursive
    """

    def setUp(self):
        """Set Up nodes for each test case"""
        super(NvmeObject, self).setUp()

        # set common params
        self.record_size = self.params.get("record_size", "/run/container/*")
        self.pool_size = self.params.get("size", "/run/pool/createsize/*")

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
        :avocado: tags=nvme,pr,hw,nvme_object_single_pool,medium,nvme_object
        """

        # perform multiple object writes to a single pool
        test_runner(self, self.pool_size[0], self.record_size[:-1])

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
        :avocado: tags=nvme,full_regression,hw,nvme_object_multiple_pools
        :avocado: tags=medium,nvme_object
        """

        # thread to perform simulatneous object writes to multiple pools
        threads = []
        for size in self.pool_size[:-1]:
            # excluding last record size in the list if size is
            # self.pool_size[0] to not over write a pool with less space
            # available.
            # pylint: disable=pointless-statement
            if size == self.pool_size[0]:
                self.record_size[:-1]
            thread = threading.Thread(target=test_runner,
                                      args=(self, size, self.record_size))
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

        #test_runner(self, self.pool_size[2], self.record_size)
