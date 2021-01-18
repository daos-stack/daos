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

import time

from ior_test_base import IorTestBase


class AggregationChecksum(IorTestBase):
    """Test class Description: Verify Aggregated extends have valid checksum.

    :avocado: recursive
    """

    def get_nvme_free_space(self):
        """ Display pool free space """
        free_space = self.pool.get_pool_free_space("nvme")
        self.log.info("Free nvme space: %s", free_space)

        return free_space

    def test_aggregationchecksum(self):
        """Jira ID: DAOS-4332.
        Test Description:
            Verify Aggregated extends have valid checksum.
        Use Cases:
            Create Pool.
            Create Container.
            Capture Free space for nvme.
            Run IOR with transfer size < 4K and block size 256M
            so that the data goes to scm.
            Allow the aggregation to finish.
            Run IOR again this time to read back the data with read verify
            option enabled.
        :avocado: tags=all,daosio,hw,small,pr,daily_regression
        :avocado: tags=aggregationchecksum
        """

        # test params
        flags = self.params.get("iorflags", "/run/ior/*")

        # Create pool and container
        self.update_ior_cmd_with_pool()

        # capture free nvme space before aggregation
        nvme_size_before_aggregation = self.get_nvme_free_space()

        # update ior options
        self.ior_cmd.flags.update(flags[0])
        # run ior write
        self.run_ior_with_pool(create_cont=False)

        # Now wait until aggregation moves all the data written by ior
        # to nvme
        counter = 1
        transfered_data = (nvme_size_before_aggregation -
                           self.get_nvme_free_space())
        while transfered_data < int(self.ior_cmd.block_size.value):
            # try to wait for 4 x 60 secs for aggregation to be completed or
            # else exit the test with a failure.
            if counter > 4:
                self.log.info("Free space before aggregation: %s",
                              nvme_size_before_aggregation)
                self.log.info("Free space when test terminated: %s",
                              self.get_nvme_free_space())
                self.fail("Failing test: Aggregation taking too long")
            time.sleep(60)
            transfered_data = (nvme_size_before_aggregation -
                               self.get_nvme_free_space())
            counter += 1

        # once entire file is aggregated to nvme, read back with verification
        self.ior_cmd.flags.update(flags[1])
        self.run_ior_with_pool(create_cont=False)
