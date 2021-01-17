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
from apricot import skipForTicket
from rebuild_test_base import RebuildTestBase


class CascadingFailures(RebuildTestBase):
    # pylint: disable=too-many-ancestors
    """Test cascading failures during rebuild.

    :avocado: recursive
    """

    CANCEL_FOR_TICKET = [["DAOS-2799", "targets", 8]]

    def __init__(self, *args, **kwargs):
        """Initialize a CascadingFailures object."""
        super(CascadingFailures, self).__init__(*args, **kwargs)
        self.mode = None

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
        if self.mode == "simultaneous":
            # Exclude both ranks from the pool to initiate rebuild
            self.pool.start_rebuild(self.inputs.rank.value, self.d_log)
        else:
            # Exclude the first rank from the pool to initiate rebuild
            self.pool.start_rebuild([self.inputs.rank.value[0]], self.d_log)

        if self.mode == "sequential":
            # Exclude the second rank from the pool
            self.pool.start_rebuild([self.inputs.rank.value[1]], self.d_log)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True, 1)

    def execute_during_rebuild(self):
        """Execute test steps during rebuild."""
        if self.mode == "cascading":
            # Exclude the second rank from the pool during rebuild
            self.pool.start_rebuild([self.inputs.rank.value[1]], self.d_log)

        # Populate the container with additional data during rebuild
        self.container.write_objects(obj_class=self.inputs.object_class.value)

    @skipForTicket("DAOS-3215")
    def test_simultaneous_failures(self):
        """Jira ID: DAOS-842.

        Test Description:
            Configure a pool with sufficient redundancy to survive and rebuild
            from two target failures.  Trigger two target failures at the same
            time.  User application I/O should continue to succeed throughout
            the rebuild process and after.  Once the rebuild is complete the
            pool should reflect a normal status.

        Use Cases:
            Verify rebuild with multiple server failures.

        :avocado: tags=all,medium,full_regression,rebuild
        :avocado: tags=multitarget,simultaneous
        """
        self.mode = "simultaneous"
        self.execute_rebuild_test()

    @skipForTicket("DAOS-6256")
    def test_sequential_failures(self):
        """Jira ID: DAOS-843.

        Test Description:
            Configure a pool with sufficient redundancy to survive and rebuild
            from two target failures.  Trigger a single target failure.  Before
            rebuilding from the first failure, activate a second failure.  User
            application I/O should continue to succeed throughout the rebuild
            process and after.  Once the rebuild is complete the pool should
            reflect a normal status.

        Use Cases:
            Verify rebuild with multiple server failures.

        :avocado: tags=all,medium,full_regression,rebuild
        :avocado: tags=multitarget,sequential
        """
        self.mode = "sequential"
        self.execute_rebuild_test()

    @skipForTicket("DAOS-6256")
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

        :avocado: tags=all,medium,full_regression,rebuild
        :avocado: tags=multitarget,cascading
        """
        self.mode = "cascading"
        self.execute_rebuild_test()
