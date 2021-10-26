#!/usr/bin/python
"""
   (C) Copyright 2020-2021 Intel Corporation.

   SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ior_test_base import IorTestBase
from dmg_utils import check_system_query_status


class IoAggregation(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify Aggregation across system shutdown.

    :avocado: recursive
    """
    def get_nvme_free_space(self):
        """Display pool free space."""
        free_space = self.pool.get_pool_free_space("nvme")
        self.log.info("Free nvme space: %s", free_space)

        return free_space

    def test_ioaggregation(self):
        """Jira ID: DAOS-3752.

        Test Description:
            Verify Aggregation across system shutdown.

        Use Cases:
            Create Pool.
            Create Container.
            Run IOR and keep the written.
            Capture Free space available after first ior write.
            Create snapshot and obtain the epoch id.
            Write to the same ior file and same amount of data,
            without overwriting the previous data.
            Capture free space again, after second ior write.
            Destroy the snapshot which was created.
            Shut down the servers and restart them again.
            After servers have successfully restarted, Look for
            aggregation to finish by checking the free space available.
            If current free space is equal to free space after first
            ior write, then pass otherwise fail the test after waiting
            for 4 attempts.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=daosio,io_aggregation,tx
        """
        # update ior signature option
        self.ior_cmd.signature.update("123")
        # run ior write process
        self.run_ior_with_pool()

        # capture free space before taking the snapshot
        self.get_nvme_free_space()

        # create snapshot
        self.container.create_snap()

        # write to same ior file again
        self.ior_cmd.signature.update("456")
        self.run_ior_with_pool(create_cont=False)

        # capture free space after second ior write
        free_space_before_snap_destroy = self.get_nvme_free_space()

        # delete snapshot
        self.container.destroy_snap(epc=self.container.epoch)

        # Shutdown the servers and restart
        self.get_dmg_command().system_stop(True)
        time.sleep(5)
        self.get_dmg_command().system_start()

        # check if all servers started as expected
        scan_info = self.get_dmg_command().system_query()
        if not check_system_query_status(scan_info):
            self.fail("One or more servers crashed")

        # Now check if the space is returned back.
        counter = 1
        returned_space = (self.get_nvme_free_space() -
                          free_space_before_snap_destroy)

        while returned_space < int(self.ior_cmd.block_size.value):
            # try to wait for 4 x 60 secs for aggregation to be completed or
            # else exit the test with a failure.
            if counter > 4:
                self.log.info("Free space before snapshot destroy: %s",
                              free_space_before_snap_destroy)
                self.log.info("Free space when test terminated: %s",
                              self.get_nvme_free_space())
                self.fail("Aggregation did not complete as expected")

            time.sleep(60)
            returned_space = (self.get_nvme_free_space() -
                              free_space_before_snap_destroy)
            counter += 1
