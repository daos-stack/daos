"""
  (C) Copyright 2019-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from rebuild_test_base import RebuildTestBase


class RbldCascadingFailures(RebuildTestBase):
    """Test cascading failures during rebuild.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CascadingFailures object."""
        super().__init__(*args, **kwargs)
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
            self.server_managers[0].stop_ranks(self.inputs.rank.value, force=True)
        else:
            # Exclude the first rank from the pool to initiate rebuild
            self.server_managers[0].stop_ranks([self.inputs.rank.value[0]], force=True)

        if self.mode == "sequential":
            # Exclude the second rank from the pool
            self.server_managers[0].stop_ranks([self.inputs.rank.value[1]], force=True)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild_to_start(1)

    def execute_during_rebuild(self):
        """Execute test steps during rebuild."""
        if self.mode == "cascading":
            # Exclude the second rank from the pool during rebuild
            self.server_managers[0].stop_ranks([self.inputs.rank.value[1]], force=True)

        self.container.set_prop(prop="status", value="healthy")
        # Populate the container with additional data during rebuild
        self.container.write_objects(obj_class=self.inputs.object_class.value)

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

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,multitarget,simultaneous
        :avocado: tags=RbldCascadingFailures,test_simultaneous_failures
        """
        self.mode = "simultaneous"
        self.execute_rebuild_test()

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

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,multitarget,sequential
        :avocado: tags=RbldCascadingFailures,test_sequential_failures
        """
        self.mode = "sequential"
        self.execute_rebuild_test()

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

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,multitarget,sequential
        :avocado: tags=RbldCascadingFailures,test_cascading_failures
        """
        self.mode = "cascading"
        self.execute_rebuild_test()
