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

#from pathos.multiprocessing import ProcessingPool
# from multiprocessing import Pool
import multiprocessing
import threading
import concurrent.futures
import avocado
import ctypes
import pickle

from daos_api import DaosApiError
from test_utils import TestPool, TestContainer
from apricot import TestWithServers


def container_write(container, record):
    """Method to write to a container"""
    print("***cotainer.opened:{}".format(container.opened))
#    for record in record_size:
    container.record_qty.update(record)
    print(container.record_qty)
    # write multiple objects
    container.write_objects()

    # read written objects and verify
    container.read_objects()

#    # invoke pool query after write
#    self.pool.get_info()

    # destroy container
#    if self.container is not None:
#        self.container.destroy()

class NvmeObject(TestWithServers):
    """Test class for NVMe storage by creating/Updating/Fetching
       large number of objects.

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

    def tearDown(self):
        """Tear Down each test case."""
        try:
            if self.container is not None:
                self.container.destroy()
            if self.pool is not None:
                self.pool.destroy(1)
        finally:
            # Stop the servers and agents
            super(NvmeObject, self).tearDown()

#    def __getstate__(self):
#        pass

#    def __getstate__(self):
#        pass
    def container_write(self, record):
        print(record_size)
#    for record in record_size:
        self.container.record_qty.update(record)
        print(self.container.record_qty)
        # write multiple objects
        self.container.write_objects()

        # read written objects and verify
        self.container.read_objects()

#    # invoke pool query after write
#    self.pool.get_info()

    # destroy container
#    if self.container is not None:
#        self.container.destroy()
    
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
        :avocado: tags=nvme,pr,hw,nvme_object_single_pool,small
        """

        # Test Params
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.container = TestContainer(self.pool)
        self.container.get_params(self)
        
        # set pool size
        self.pool.nvme_size.update(self.pool_size[0])
        # Create a pool
        self.pool.create()
        self.pool.connect()

        # invoke pool query before write
        self.pool.get_info()

        # create container
        self.container.create()
        self.container.open()
        #with concurrent.futures.ThreadPoolExecutor(max_workers=3) as executor:
        #    executor.map(container_write, [self.container, self.record_size[0]])
        threads = []
        for rec in self.record_size[:-1]:
            for t in range(3):
                thread = threading.Thread(target=container_write, args=(self.container, rec))
                threads.append(thread)
#            thread.start()
#            thread.join()

        for job in threads:
            job.start()
#
        for job in threads:
            job.join()

#        thread.start()
#        thread.join()
#        print(self.record_size[:-1])
#        print("***{}***".format(ctypes.cast(ctypes.addressof(self.container)).contents))
#        mp = Pool(processes=2)
#        mp.apply_async(container_write, (container, self.record_size[2]))
#        jobs = []
#        for i in range(1):
#            process = multiprocessing.Process(target=container_write, args=(self.container, self.record_size[0]))
#            jobs.append(process)
#        for j in jobs:
#            j.start()
#        for j in jobs:
#            j.join()
#        container_write(container, self.record_size[1])
#        for record in self.record_size[:-1]:
#            self.container.record_qty.update(record)
#            print(self.container.record_qty)
#            # write multiple objects
#            self.container.write_objects()

#        # read written objects and verify
#        self.container.read_objects()

        # invoke pool query after write
        self.pool.get_info()

        # destroy container
        if self.container is not None:
            self.container.destroy()

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
        :avocado: tags=nvme,full_regression,hw,nvme_object_multiple_pools,small
        """

        for size in self.pool_size:
            # Test Params
            self.pool = TestPool(self.context, self.log)
            self.pool.get_params(self)
            self.container = TestContainer(self.pool)
            self.container.get_params(self)

            # set pool size
            self.pool.nvme_size.update(size)
            # Create a pool
            self.pool.create()
            self.pool.connect()

            # invoke pool query before write
            self.pool.get_info()

            # create container
            self.container.create()
            print(self.record_size)
            for record in self.record_size:
                self.container.record_qty.update(record)
                print(self.container.record_qty)
                # write multiple objects
                self.container.write_objects()

            # read written objects and verify
            self.container.read_objects()

            # invoke pool query after write
            self.pool.get_info()

            # destroy container
            if self.container is not None:
                self.container.destroy()

            # destroy pool
            if self.pool is not None:
                self.pool.destroy(1)
