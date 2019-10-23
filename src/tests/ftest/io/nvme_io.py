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

from pydaos.raw import DaosPool, DaosApiError
from ior_test_base import IorTestBase


class NvmeIo(IorTestBase):
    """Test class for NVMe with IO tests.

    Test Class Description:
        Test the general Metadata operations and boundary conditions.

    :avocado: recursive
    """

    @avocado.fail_on(DaosApiError)
    def test_nvme_io(self):
        """Jira ID: DAOS-2082.

        Test Description:
            Test will run IOR with standard and non standard sizes.  IOR will
            be run for all Object type supported. Purpose is to verify pool
            size (SCM and NVMe) for IOR file.

        Use Cases:
            Running multiple IOR on same server start instance.

        :avocado: tags=all,daosio,full_regression,hw,nvme_io
        """
        # Pool params
        pool_mode = self.params.get("mode", '/run/pool/createmode/*')
        pool_uid = os.geteuid()
        pool_gid = os.getegid()
        pool_group = self.params.get("setname", '/run/pool/createset/*')
        pool_svcn = self.params.get("svcn", '/run/pool/createsvc/')

        # Test params
        tests = self.params.get("ior_sequence", '/run/ior/*')
        object_type = self.params.get("object_type", '/run/ior/*')

        # Loop for every IOR object type
        for obj_type in object_type:
            for ior_param in tests:
                # There is an issue with NVMe if Transfer size>64M,
                # Skipped this sizes for now
                if ior_param[2] > 67108864:
                    self.log.warning("Xfersize > 64M fails - DAOS-1264")
                    continue

                # Create and connect to a pool
                self.pool = DaosPool(self.context)
                self.pool.create(
                    pool_mode, pool_uid, pool_gid, ior_param[0], pool_group,
                    svcn=pool_svcn, nvme_size=ior_param[1])
                self.pool.connect(1 << 1)

                # Get the current pool sizes
                size_before_ior = self.pool.pool_query()

                # Run ior with the parameters specified for this pass
                self.ior_cmd.transfer_size.update(ior_param[2])
                self.ior_cmd.block_size.update(ior_param[3])
                self.ior_cmd.daos_oclass.update(obj_type)
                self.ior_cmd.set_daos_params(self.server_group, self.pool)
                self.run_ior(self.get_job_manager_command(), ior_param[4])

                # Verify IOR consumed the expected amount ofrom the pool
                self.verify_pool_size(size_before_ior, ior_param[4])

                try:
                    if self.pool:
                        self.pool.disconnect()
                        self.pool.destroy(1)
                except DaosApiError as error:
                    self.log.error(
                        "Pool disconnect/destroy error: %s", str(error))
                    self.fail("Failed to Destroy/Disconnect the Pool")
