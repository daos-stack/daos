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


class RebuildDeleteObjects(TestWithServers):
    """Test class for deleting objects during pool rebuild.

    Test Class Description:
        This class contains tests for deleting objects from a container during
        rebuild.

    :avocado: recursive
    """

    def test_rebuild_delete_objects(self):
        """JIRA ID: DAOS-2572.

        Test Description:
            Delete objects during rebuild. Rebuild should complete successfully
            and only the remaining data should be accessible and it should only
            exist on the rebuild target and non-excluded, original targets. The
            data in the deleted objects should not be accessible.

        Use Cases:
            foo

        :avocado: tags=all,medium,full_regression,rebuild,rebuilddeleteobject
        """
        # Get the test params
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        container = TestContainer(self.pool)
        container.get_params(self)
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank", "/run/testparams/*")
        obj_class = self.params.get("object_class", "/run/testparams/*")
        server_count = len(self.hostlist_servers)

        # Create a pool
        self.pool.create()

        # Verify the pool information before rebuild
        info_checks = {
            "pi_nnodes": server_count,
            "pi_ntargets": (server_count * targets),
            "pi_ndisabled": 0,
        }
        rebuild_checks = {
            "rs_done": 1,
            "rs_obj_nr": 0,
            "rs_rec_nr": 0,
            "rs_errno": 0,
        }
        status = self.pool.check_pool_info(**info_checks)
        status &= self.pool.check_rebuild_status(**rebuild_checks)
        self.assertTrue(status, "Error confirming pool info before rebuild")

        # Create a container and write objects
        container.create()
        container.write_objects(rank, obj_class)

        # Verify the rank to be excluded has at least one object
        pre_rank_list = container.get_target_rank_lists(" before rebuild")
        obj_with_rank = container.get_target_rank_count(rank, pre_rank_list)
        self.assertGreater(
            obj_with_rank, 0, "No objects written to rank {}".format(rank))

        # Exclude the rank from the pool to initiate rebuild
        self.pool.start_rebuild(self.server_group, rank, self.d_log)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True, 1)

        # Delete half of the objects from the container
        obj_indices = [
            index for index in range(container.object_qty.value) if index % 2]
        punched_objs = container.punch_objects(obj_indices)

        # Confirm rebuild completes
        self.pool.wait_for_rebuild(False, 1)

        # Verify the expected number of objects were punched
        self.assertEqual(
            len(obj_indices), punched_objs,
            "Error punching objects during rebuild: {}/{}".format(
                punched_objs, len(obj_indices)))

        # Verify the excluded rank is no longer used with the objects
        new_rank_list = container.get_target_rank_lists(" after rebuild")
        obj_with_rank = container.get_target_rank_count(rank, new_rank_list)
        self.assertEqual(
            obj_with_rank, 0, "No objects written to rank {}".format(rank))

        # Verify the pool information after rebuild
        info_checks["pi_ndisabled"] = ">0"
        rebuild_checks["rs_obj_nr"] = ">0"
        rebuild_checks["rs_rec_nr"] = ">0"
        status = self.pool.check_pool_info(**info_checks)
        status &= self.pool.check_rebuild_status(**rebuild_checks)
        self.assertTrue(status, "Error confirming pool info after rebuild")

        # Verify the deleted objects cannot be read and the other objects can
        # be read
        self.log.info(
            "Verifying %s original objects can still be read and %s deleted "
            "objects cannot be read",
            len(container.written_data) - len(obj_indices), len(obj_indices))
        for index, data in enumerate(container.written_data):
            expected = index not in obj_indices
            if expected != data.read_object(container):
                self.log.error("  Unexpected result")
                status = False
        self.assertTrue(status, "Error reading objects after rebuild")

        self.log.info("Test passed")
