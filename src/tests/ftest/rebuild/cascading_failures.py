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

from apricot import TestWithServers
from test_utils import TestPool, TestContainer


class CascadingFailures(TestWithServers):
    """Test cascading failures during rebuild.

    :avocado: recursive
    """

    def test_cascading_failures(self):
        """Jira ID: DAOS-844.

        Test Description:
            Configure a pool with sufficient redundancy to survive and rebuild
            from two target failures.  Trigger a single target failure.  While
            rebuilding from the first failure, activate a second failure.  User
            application I/O should continue to succeed throughout the rebuild
            process and after.  Once the rebuild is complete the pool should
            reflect a normal status.

        Use Cases:
            ?

        :avocado: tags=rebuild,cascadingfailure
        """
        # Get the test parameters
        pool = TestPool(self.context, self.log)
        pool.get_params(self)
        container = TestContainer(pool)
        container.get_params(self)
        targets = self.params.get("targets", "/run/server_config/*")
        obj_class = self.params.get("obj_class", "/run/test/*")
        rank_index = self.params.get("rank_index", "/run/test/*")
        node_qty = len(self.hostlist_servers)

        # Verify there are enough servers for the requested replica
        min_required = 5 if obj_class == "OC_RP_3G1" else 4
        if node_qty < min_required:
            self.cancel(
                "Not enough servers ({}) for object class {}".format(
                    min_required, obj_class))

        # Create and connect to a pool
        pool.create()
        pool.connect()

        # Create and open a container
        container.create()

        # Populate the container with data
        container.write_objects(obj_class=obj_class)

        # Determine which ranks have replicas of the data
        target_rank_lists = container.get_target_rank_lists(" before rebuild")
        exclude_rank_list = target_rank_lists[rank_index]
        self.log.info("Excluding cascading ranks: %s", exclude_rank_list)

        # Setup the pool info checks
        info_checks = {
            "pi_uuid": pool.uuid,
            "pi_ntargets": node_qty * targets,
            "pi_nnodes": node_qty,
            "pi_ndisabled": 0,
        }
        rebuild_checks = {
            "rs_errno": 0,
            "rs_done": 1,
            "rs_obj_nr": 0,
            "rs_rec_nr": 0,
        }
        rebuild_objs = 0

        # Verify the pool info and start rebuild
        for index in range(2):
            # Count the number of objects written to this rank
            rebuild_objs = container.get_target_rank_count(
                target_rank_lists, exclude_rank_list[index])
            self.log.info(
                "Expecting %s rebuilt objects for rank %s",
                rebuild_objs, index)

            # Check the pool info
            status = pool.check_pool_info(**info_checks)
            status &= pool.check_rebuild_status(**rebuild_checks)
            self.assertTrue(
                status,
                "Error verifying pool info prior to excluding rank {}".format(
                    exclude_rank_list[index]))

            # Exclude the next rank with replica data
            pool.start_rebuild(
                self.server_group, exclude_rank_list[index], self.d_log)

            # Update the checks to reflect the ongoing rebuild
            info_checks["pi_ndisabled"] += targets
            rebuild_checks["rs_done"] = 0
            rebuild_checks["rs_obj_nr"] = ">=0"
            rebuild_checks["rs_rec_nr"] = ">=0"

            if index == 0:
                # Wait for rebuild to start
                pool.wait_for_rebuild(True, 1)
            else:
                # Check the pool info
                status = pool.check_pool_info(**info_checks)
                status &= pool.check_rebuild_status(**rebuild_checks)
                self.assertTrue(
                    status,
                    "Error verifying pool info after excluding rank {}".format(
                        exclude_rank_list[index]))

        # Populate the container with data during rebuild
        container.write_objects(obj_class=obj_class)

        # Wait for rebuild to complete
        pool.wait_for_rebuild(False, 1)

        # # Verify all the data can be read after rebuild
        # status = container.read_objects()
        # self.assertTrue(status, "Error reading the data after rebuild")

        # Display the updated target rank list
        container.get_target_rank_lists(" after rebuild")

        # Check the pool info
        rebuild_checks["rs_done"] = 1
        rebuild_checks["rs_obj_nr"] = rebuild_objs
        rebuild_checks["rs_rec_nr"] = \
            rebuild_checks["rs_obj_nr"] * container.record_qty.value
        status = pool.check_pool_info(**info_checks)
        status &= pool.check_rebuild_status(**rebuild_checks)
        self.assertTrue(status, "Error verifying pool info after rebuild")
        self.log.info("Test passed")
