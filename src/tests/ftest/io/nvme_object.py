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

import os
import avocado

from daos_api import DaosPool, DaosApiError
from ior_test_base import IorTestBase
from test_utils import TestPool, TestContainer, TestContainerData
from apricot import TestWithServers

class NvmeObject(TestWithServers):
    """Test class for NVMe storage by creating/Updating/Fetching
       large number of objects.

    Test Class Description:
        Test the general functional operations of objects on nvme storage
        i.e. Creation/Updating/Fetching

    :avocado: recursive
    """

    def tearDown(self):
        """Tear Down each test case."""
        try:
            if self.container is not None:
                self.container.destroy()
            if self.pool is not None and self.pool.pool.attached:
                self.pool.destroy(1)
        finally:
            # Stop the servers and agents
            super(NvmeObject, self).tearDown()

    @avocado.fail_on(DaosApiError)
    def test_nvme_object(self):
        """Jira ID: DAOS-2087.

        Test Description:
            Test will create pool on nvme using TestPool
            Create large number of objects
            Update/Fetch with different object ID in single pool

        Use Cases:
            Verify the objects are being created and the data is not
            corrupted.
        :avocado: tags=nvme,nvme_object,medium
        """
        record_size = self.params.get("record_size", "/run/container/*")
        pool_size = self.params.get("size", "/run/pool/createsize/*")
        for size in pool_size:
            # Test Params
            self.pool = TestPool(self.context, self.log) 
            self.pool.get_params(self)
            self.container = TestContainer(self.pool)
            self.container.get_params(self)

#        for size in pool_size:
            self.pool.scm_size.update(size)
            # Create a pool
            self.pool.create()
            self.pool.connect()

            # create container
            self.container.create()
            print(record_size)
            for record in record_size:
                self.container.record_qty.update(record)
                print(self.container.record_qty)
                # write multiple objects
                self.container.write_objects()

                # read written objects and verify
                self.container.read_objects()
