#!/usr/bin/python
'''
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
'''
from __future__ import print_function

import os
import avocado

from apricot import TestWithServers
from daos_api import DaosPool, DaosApiError
from general_utils import DaosTestError
from ior_utils import IorCommand, IorFailed
import write_host_file


class NvmeIo(TestWithServers):
    """Test class for NVMe with IO tests.

    Test Class Description:
        Test the general Metadata operations and boundary conditions.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super(NvmeIo, self).__init__(*args, **kwargs)
        self.ior_cmd = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(NvmeIo, self).setUp()

        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file.write_host_file(
                self.hostlist_clients, self.workdir, None)

        # Get the parameters for IOR
        self.ior_cmd = IorCommand()
        self.ior_cmd.set_params(self)

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.pool is not None and self.pool.attached:
                self.pool.destroy(1)
        finally:
            # Stop the servers and agents
            super(NvmeIo, self).tearDown()

    def verify_pool_size(self, original_pool_info, processes):
        """Validate the pool size.

        Args:
            original_pool_info (PoolInfo): Pool info prior to IOR
            processes (int): number of processes

        Raises:
            DaosTestError: if there is any error obtaining the pool size

        """
        # Get the current pool size for comparison
        current_pool_info = self.pool.pool_query()

        # If Transfer size is < 4K, Pool size will verified against NVMe, else
        # it will be checked against SCM
        if self.ior_cmd.transfer_size.value >= 4096:
            print("Size is > 4K,Size verification will be done with NVMe size")
            storage_index = 1
        else:
            print("Size is < 4K,Size verification will be done with SCM size")
            storage_index = 0
        free_pool_size = \
            original_pool_info.pi_space.ps_space.s_free[storage_index] - \
            current_pool_info.pi_space.ps_space.s_free[storage_index]

        expected_pool_size = self.ior_cmd.get_aggregate_total(processes)
        if free_pool_size < expected_pool_size:
            raise DaosTestError(
                'Pool Free Size did not match Actual = {} Expected = {}'
                .format(free_pool_size, expected_pool_size))

    @avocado.fail_on(DaosApiError)
    def test_nvme_io(self):
        """Jira ID: DAOS-2082.

        Test Description:
            Test will run IOR with standard and non standard sizes.  IOR will
            be run for all Object type supported. Purpose is to verify pool
            size (SCM and NVMe) for IOR file.

        Use Cases:
            Running multiple IOR on same server start instance.

        :avocado: tags=nvme,nvme_io,large
        """
        # Pool params
        pool_mode = self.params.get("mode", '/run/pool/createmode/*')
        pool_uid = os.geteuid()
        pool_gid = os.getegid()
        pool_group = self.params.get("setname", '/run/pool/createset/*')
        pool_svcn = self.params.get("svcn", '/run/pool/createsvc/')

        tests = self.params.get("ior_sequence", '/run/ior/*')
        object_type = self.params.get("object_type", '/run/ior/*')
        # Loop for every IOR object type
        for obj_type in object_type:
            for ior_param in tests:
                # There is an issue with NVMe if Transfer size>64M,
                # Skipped this sizes for now
                if ior_param[2] > 67108864:
                    print("Xfersize > 64M getting failed, DAOS-1264")
                    continue

                self.pool = DaosPool(self.context)
                self.pool.create(
                    pool_mode, pool_uid, pool_gid, ior_param[0], pool_group,
                    svcn=pool_svcn, nvme_size=ior_param[1])
                self.pool.connect(1 << 1)

                size_before_ior = self.pool.pool_query()

                self.ior_cmd.set_daos_params(self.server_group, self.pool)
                self.ior_cmd.transfer_size.value = ior_param[2]
                self.ior_cmd.block_size.value = ior_param[3]
                self.ior_cmd.daos_oclass.value = obj_type
                try:
                    self.ior_cmd.run(
                        self.basepath, ior_param[4], self.hostfile_clients)
                except IorFailed as error:
                    print(error)
                    self.fail("Failed running IOR")

                self.verify_pool_size(size_before_ior, ior_param[4])

                try:
                    if self.pool:
                        self.pool.disconnect()
                        self.pool.destroy(1)
                except DaosApiError as error:
                    print(error)
                    self.fail("Failed to Destroy/Disconnect the Pool")
