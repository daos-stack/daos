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
        self.__mode = None

    def verify_rank_has_objects(self, container, ranks):
        """Verify the first rank to be excluded has at least one object.

        Args:
            container (TestContainer): container to verify
            ranks (int/list): single rank or list of ranks to verify
        """
        if not isinstance(ranks, list):
            ranks = [ranks]
        rank_list = container.get_target_rank_lists(" before rebuild")
        objects = {
            rank: container.get_target_rank_count(rank, rank_list)
            for rank in ranks
        }
        self.assertGreater(
            objects[ranks[0]], 0,
            "No objects written to rank {}".format(ranks[0]))

    def start_rebuild(self, pool, ranks):
        """Start the rebuild process.

        Args:
            pool (TestPool): pool to start rebuild on
            ranks (int/list): single rank or list of ranks to stop
        """
        if not isinstance(ranks, list):
            ranks = [ranks]

        if self.__mode == "simultaneous":
            # Exclude both ranks from the pool to initiate rebuild
            self.server_managers[0].stop_ranks(ranks, force=True)
        else:
            # Exclude the first rank from the pool to initiate rebuild
            self.server_managers[0].stop_ranks([ranks[0]], force=True)

        if self.__mode == "sequential":
            # Exclude the second rank from the pool
            self.server_managers[0].stop_ranks([ranks[1]], force=True)

        # Wait for rebuild to start
        pool.wait_for_rebuild_to_start(1)

    def execute_during_rebuild(self, container):
        """Execute test steps during rebuild.

        Args:
            container (TestContainer): container to write data to
        """
        if self.__mode == "cascading":
            # Exclude the second rank from the pool during rebuild
            self.server_managers[0].stop_ranks([self.inputs.rank.value[1]], force=True)

        container.set_prop(prop="status", value="healthy")
        # Populate the container with additional data during rebuild
        container.write_objects(obj_class=self.inputs.object_class.value)

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
        self.__mode = "simultaneous"
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
        self.__mode = "sequential"
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
        self.__mode = "cascading"
        self.execute_rebuild_test()
