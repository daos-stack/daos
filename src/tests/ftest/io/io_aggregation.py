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
from dmg_utils import check_system_query_status
from daos_utils import DaosCommand


class IoAggregation(IorTestBase):
    """Test class Description: Verify Aggregation across system shutdown.

    :avocado: recursive
    """

    def setUp(self):
        """Set up test before executing"""
        super(IoAggregation, self).setUp()
        self.dmg = self.get_dmg_command()
        self.daos_cmd = DaosCommand(self.bin)

    def get_nvme_free_space(self):
        """ Display pool free space """
        free_space = self.pool.get_pool_free_space("nvme")
        self.log.info("Free nvme space: %s", free_space)

        return free_space

    def highest_epoch(self, kwargs):
        """Returns Highest Epoch for the container

        Args:
          kwargs (dict): Dictionary of arguments to be passed to
                         container_query method.

        Returns:
          Highest epoch value for a given container.
        """
        highest_epoch = self.daos_cmd.get_output(
            "container_query", **kwargs)[0][4]

        return highest_epoch

    def test_ioaggregation(self):
        """Jira ID: DAOS-4332.
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
            Capture Highest epoch ID before snapshot destroy.
            Destroy the snapshot which was created.
            Shut down the servers and restart them again.
            After servers have successfully restarted, Look for
            aggregation to finish by checking the free space available
            and value of highest epoch which should be higher than
            the value of highest epoch before snapshot destroy.
            If current free space is equal to free space after first
            ior write, then pass otherwise fail the test after waiting
            for 4 attempts.
        :avocado: tags=all,daosio,hw,small,full_regression,ioaggregation
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

        # obtain highest epoch before snapshot destroy via container query
        kwargs = {
            "pool": self.pool.uuid,
            "svc": self.pool.svc_ranks[0],
            "cont": self.container.uuid
        }
        highest_epc_before_snap_destroy = self.highest_epoch(kwargs)

        # delete snapshot
        self.container.destroy_snap(epc=self.container.epoch)

        # Shutdown the servers and restart
        self.get_dmg_command().system_stop(True)
        time.sleep(5)
        self.get_dmg_command().system_start()

        # check if all servers started as expected
        scan_info = self.get_dmg_command().get_output("system_query")
        if not check_system_query_status(scan_info):
            self.fail("One or more servers crashed")

        # Now check if the space is returned back and Highest epoch value
        # is higher than the the value just before snapshot destroy.
        counter = 1
        returned_space = (self.get_nvme_free_space() -
                          free_space_before_snap_destroy)
        while returned_space < int(self.ior_cmd.block_size.value) or \
            highest_epc_before_snap_destroy >= self.highest_epoch(kwargs):
            # try to wait for 4 x 60 secs for aggregation to be completed or
            # else exit the test with a failure.
            if counter > 4:
                self.log.info("Free space before snapshot destroy: %s",
                              free_space_before_snap_destroy)
                self.log.info("Free space when test terminated: %s",
                              self.get_nvme_free_space())
                self.log.info("Highest Epoch before IO Aggregation: %s",
                              highest_epc_before_snap_destroy)
                self.log.info("Highest Epoch when test terminated: %s",
                              self.highest_epoch(kwargs))
                self.fail("Aggregation did not complete as expected")
            time.sleep(60)
            returned_space = (self.get_nvme_free_space() -
                              free_space_before_snap_destroy)
            counter += 1
