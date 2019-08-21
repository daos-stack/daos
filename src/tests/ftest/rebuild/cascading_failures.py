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
from rebuild_test_base import RebuldTestBase


class CascadingFailures(RebuldTestBase):
    """Test cascading failures during rebuild.

    :avocado: recursive
    """

    def create_test_container(self):
        """Create a container and write objects."""
        self.container.create()
        self.container.write_objects(
            self.inputs.rank.value[0], self.inputs.object_class.value)

    def verify_rank_has_objects(self):
        """Verify the first rank to be excluded has at least one object."""
        rank_list = self.container.get_target_rank_lists(" before rebuild")
        objects = {
            rank: self.container.get_target_rank_count(rank, rank_list)
            for rank in self.inputs.rank.value
        }
        self.assertGreater(
            objects[self.inputs.rank.value[0]], 0,
            "No objects written to rank {}".format(self.inputs.rank.value[0]))

    def verify_rank_has_no_objects(self):
        """Verify the excluded rank has zero objects."""
        rank_list = self.container.get_target_rank_lists(" after rebuild")
        objects = {
            rank: self.container.get_target_rank_count(rank, rank_list)
            for rank in self.inputs.rank.value
        }
        for rank in self.inputs.rank.value:
            self.assertEqual(
                objects[rank], 0,
                "Excluded rank {} still has objects".format(rank))

    def start_rebuild(self):
        """Start the rebuild process."""
        # Exclude the first rank from the pool to initiate rebuild
        self.pool.start_rebuild(
            self.server_group, self.inputs.rank.value[0], self.d_log)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True, 1)

    def execute_during_rebuild(self):
        """Execute test steps during rebuild."""
        # Exclude the second rank from the pool during rebuild
        self.pool.start_rebuild(
            self.server_group, self.inputs.rank.value[1], self.d_log)

        # Populate the container with additional data during rebuild
        self.container.write_objects(obj_class=self.inputs.object_class.value)

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
            Verify rebuild with multiple server failures.

        :avocado: tags=rebuild,cascadingfailure
        """
        self.execute_rebuild_test()
