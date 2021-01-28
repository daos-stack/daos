#!/usr/bin/python
"""
   (C) Copyright 2020-2021 Intel Corporation.

   SPDX-License-Identifier: BSD-2-Clause-Patent
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
