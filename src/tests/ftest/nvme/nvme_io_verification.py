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

import avocado

from pydaos.raw import DaosApiError
from test_utils_pool import TestPool
from ior_test_base import IorTestBase
from dmg_utils import check_system_query_status

class NvmeIoVerification(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for NVMe with IO tests.

    Test Class Description:
        Test IO on nvme with different pool sizes and different data size.

    :avocado: recursive
    """

    @avocado.fail_on(DaosApiError)
    def test_nvme_io_verification(self):
        """Jira ID: DAOS-2649.

        Test Description:
            Test will run IOR with non standard transfer sizes for different
            set of pool sizes. Purpose is to verify io transaction to scm and
            nvme for different pool sizes under different situations.

        Use Cases:
            (1) Running IOR with different set of transfer size where first
            transfer size is < 4096 and then > 4096. Verify that data goes to
            scm if transfer size < 4096 and then it goes to nvme if transfer
            size is > 4096.
            (2) Repeat the case(1) with maximum nvme pool size that can be
            created.
            (3) Running IOR with different set of transfer size where the
            transfer size is > 4096 throughout. Verify that data goes to nvme
            as transfer size is > 4096.
            (4) Repeat the case(3) with maximum nvme pool size that can be
            created.
        :avocado: tags=all,full_regression,hw,large,daosio,nvme_io_verification
        """
        # Test params
        tests = self.params.get("ior_sequence", '/run/ior/*')
        processes = self.params.get("np", '/run/ior/*')
        transfer_size = self.params.get("tsize", '/run/ior/transfersize/*/')
        block_size = self.ior_cmd.block_size.value

        # Loop for every IOR object type
        for ior_param in tests:
            # Create and connect to a pool
            self.pool = TestPool(
                self.context, dmg_command=self.get_dmg_command())
            self.pool.get_params(self)

            # update pool sizes
            self.pool.scm_size.update(ior_param[0])
            self.pool.nvme_size.update(ior_param[1])

            # Create a pool
            self.pool.create()

            # get pool info
            self.pool.get_info()

            for tsize in transfer_size:
                # Get the current pool sizes
                size_before_ior = self.pool.info

                # Run ior with the parameters specified for this pass
                self.ior_cmd.transfer_size.update(tsize)
                # if transfer size is less thank 1K
                # update block size to 32K to keep it small
                if tsize <= 1000:
                    self.ior_cmd.block_size.update(32000)
                else:
                    self.ior_cmd.block_size.update(block_size)
                self.ior_cmd.set_daos_params(self.server_group, self.pool)
                self.run_ior(self.get_ior_job_manager_command(), processes)

                # Verify IOR consumed the expected amount from the pool
                self.verify_pool_size(size_before_ior, self.processes)

            # destroy pool
            self.destroy_pools(self.pool)

    @avocado.fail_on(DaosApiError)
    def test_nvme_server_restart(self):
        """Jira ID: DAOS-2650.

        Test Description:
            Test will run IOR with non standard transfer sizes for different
            set of pool sizes. Purpose is to verify io transaction to scm and
            nvme for different pool sizes when servers are restarted after
            write.

        Use Cases:
            (1) Running IOR with different set of transfer size where first
            transfer size is < 4096 and then > 4096. Verify the data after
            servers are restarted.
            (2) Repeat the case(1) with maximum nvme pool size that can be
            created.
            (3) Running IOR with different set of transfer size where the
            transfer size is > 4096 throughout. Verify the data after
            servers are restarted.
            (4) Repeat the case(3) with maximum nvme pool size that can be
            created.
        :avocado: tags=all,full_regression,hw,large,daosio,nvme_server_restart
        """
        # Test params
        tests = self.params.get("ior_sequence", '/run/ior/*')
        processes = self.params.get("np", '/run/ior/*')
        transfer_size = self.params.get("tsize", '/run/ior/transfersize/*/')
        flag_write = self.params.get("write", '/run/ior/*/')
        flag_read = self.params.get("read", '/run/ior/*/')
        block_size = self.ior_cmd.block_size.value

        # Loop for every IOR object type
        for ior_param in tests:
            # Create and connect to a pool
            self.pool = TestPool(
                self.context, dmg_command=self.get_dmg_command())
            self.pool.get_params(self)

            # update pool sizes
            self.pool.scm_size.update(ior_param[0])
            self.pool.nvme_size.update(ior_param[1])

            # Create a pool
            self.pool.create()

            # get pool info
            self.pool.get_info()

            for tsize in transfer_size:
                # Run ior with the parameters specified for this pass
                self.ior_cmd.transfer_size.update(tsize)
                self.ior_cmd.flags.update(flag_write)
                # if transfer size is less thank 1K
                # update block size to 32K to keep it small
                if tsize <= 1000:
                    self.ior_cmd.block_size.update(32000)
                else:
                    self.ior_cmd.block_size.update(block_size)
                self.ior_cmd.set_daos_params(self.server_group, self.pool)
                self.run_ior(self.get_ior_job_manager_command(), processes)

                # Stop all servers
                self.get_dmg_command().system_stop(True)

                # Start all servers
                self.get_dmg_command().system_start()

                # check if all servers started as expected
                scan_info = self.get_dmg_command().get_output("system_query")
                if not check_system_query_status(scan_info):
                    self.fail("One or more servers crashed")

                # read all the data written before server restart
                self.ior_cmd.flags.update(flag_read)
                self.run_ior(self.get_ior_job_manager_command(), processes)

            # destroy pool
            self.destroy_pools(self.pool)
