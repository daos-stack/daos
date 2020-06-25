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
from general_utils import parse_log_file


class IoAggregation(IorTestBase):
    """Test class Description: Verify Aggregation across system shutdown.

    :avocado: recursive
    """

    def setUp(self):
        """Set up test before executing"""
        super(IoAggregation, self).setUp()
        self.dmg = self.get_dmg_command()

    def display_free_space(self):
        """ Display pool free space """
        free_space = self.pool.get_pool_free_space("nvme")
        self.log.info("Free nvme space: %s", free_space)

        return free_space

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
            Destroy the snapshot which was created.
            Check for Aggregation to start in server logs after
            snapshot destroy.
            Once established that aggregation has started, shut down
            the servers and restart them again.
            After servers have successfully restarted, Look for
            aggregation to finish and check the free space available
            now after aggregation has finished.
            If current free space is equal to free space after first
            ior write, then pass otherwise fail the test.
        :avocado: tags=all,daosio,hw,small,full_regression,ioaggregation
        """

        # update ior signature option
        self.ior_cmd.signature.update("123")
        # run ior write process
        self.run_ior_with_pool()

        # capture free space before taking the snapshot
        free_space_before_snap = self.display_free_space()

        # create snapshot
        self.container.create_snap()

        # write to same ior file again
        self.ior_cmd.signature.update("456")
        self.run_ior_with_pool(create_cont=False)

        # capture free space after second ior write
        free_space_after_second_write = self.display_free_space()

        # delete snapshot
        self.container.destroy_snap(epoch=self.container.epoch)

        # Check for aggregation to begin after snapshot destroy.
        # For that, first identify the line in logs where snapshot is deleted.
        server_log_path = self.log_dir + "/" + self.server_log
        line_num_snap_destroy = parse_log_file(server_log_path,
                                               "deleted snapshot", 1)[0]
        # Then, look for a specific pattern in log file after snapshot
        # deletion line.
        regex = (r"cont_child_aggregate\(\)\s+[0-9a-z\/]+\[\d\]:\s+"
                 + r"[A-Za-z]+\s+{\d+\s+\->\s+\d+}")
        counter = 1
        attempts = 3
        while not (parse_log_file(server_log_path, regex, 1,
                                  from_line=line_num_snap_destroy))[1]:
            if counter > attempts:
                self.fail("Either aggregation did not start in timely manner "
                          "or regex pattern to check aggregation in logs has "
                          "changed")
                break
            time.sleep(10)
            counter += 1

        # Once aggregation has started, shutdown the servers and restart
        self.get_dmg_command().system_stop(True)
        time.sleep(5)
        self.get_dmg_command().system_start()

        # check if all servers started as expected
        scan_info = self.get_dmg_command().get_output("system_query")
        if not check_system_query_status(scan_info):
            self.fail("One or more servers crashed")

        # check if aggregation finished successfully and expected space is
        # returned back.
        # For that, capture the line in logs where servers were restarted
        # successfully.
        line_num = parse_log_file(server_log_path, "start MS", 2)[0]
        # Now look for the pattern and check space returned match until both
        # conditions are satisfied.
        counter = 1
        while free_space_before_snap != self.display_free_space() or \
            not (parse_log_file(server_log_path, "Aggregating finished",
                                counter, from_line=line_num))[1]:
            if counter > attempts:
                self.log.info("Free space after Second write: %s",
                              free_space_after_second_write)
                self.log.info("Final Free space: %s", self.display_free_space())
                self.fail("Aggregation not completing as expected")
            time.sleep(60)
            counter += 1
