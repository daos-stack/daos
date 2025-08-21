"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from command_utils_base import BasicParameter, ObjectWithParameters


class RebuildTestParams(ObjectWithParameters):
    # pylint: disable=too-few-public-methods
    """Class for gathering test parameters."""

    def __init__(self):
        """Initialize a RebuildTestParams object."""
        super().__init__("/run/rebuild/*")
        self.object_class = BasicParameter(None)
        self.rank = BasicParameter(None)


class RebuildTestBase(TestWithServers):
    """Base rebuild test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a RebuildTestBase object."""
        super().__init__(*args, **kwargs)
        self.inputs = RebuildTestParams()

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the test parameters
        self.inputs.get_params(self)

    def verify_pool_info_before_rebuild(self, pool):
        """Verify the pool information before rebuild.

        Args:
            pool (TestPool): pool to verify
        """
        info_checks = {
            "pi_uuid": pool.uuid,
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ntargets": (
                len(self.hostlist_servers) * self.server_managers[0].get_config_value("targets")),
            "pi_ndisabled": 0,
        }
        rebuild_checks = {
            "rs_state": 1,
            "rs_obj_nr": 0,
            "rs_rec_nr": 0,
            "rs_errno": 0,
        }
        status = pool.check_pool_info(**info_checks)
        status &= pool.check_rebuild_status(**rebuild_checks)
        if not status:
            self.fail("Error confirming pool info before rebuild")

    def verify_pool_info_after_rebuild(self, pool):
        """Verify the pool information after rebuild.

        Args:
            pool (TestPool): pool to verify
        """
        info_checks = {
            "pi_uuid": pool.uuid,
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ntargets": (
                len(self.hostlist_servers) * self.server_managers[0].get_config_value("targets")),
            "pi_ndisabled": ">0",
        }
        rebuild_checks = {
            "rs_state": 2,
            "rs_obj_nr": ">=0",
            "rs_rec_nr": ">=0",
            "rs_errno": 0,
        }
        status = pool.check_pool_info(**info_checks)
        status &= pool.check_rebuild_status(**rebuild_checks)
        if not status:
            self.fail("Error confirming pool info after rebuild")

    def create_test_container(self, pool, rank):
        """Create a container and write objects.

        Args:
            pool (TestPool) pool to create the container in
            rank (int): rank to write data to

        Returns:
            TestContainer: the container created
        """
        container = self.get_container(pool)
        container.write_objects(rank, self.inputs.object_class.value)
        return container

    def verify_rank_has_objects(self, container, ranks):
        """Verify the rank to be excluded has at least one object.

        Args:
            container (TestContainer): container to verify
            ranks (int/list): single rank or list of ranks to verify
        """
        if not isinstance(ranks, list):
            ranks = [ranks]
        rank_list = container.get_target_rank_lists(" after rebuild")
        objects = {
            rank: container.get_target_rank_count(rank, rank_list)
            for rank in ranks
        }
        for rank in ranks:
            self.assertGreater(objects[rank], 0, f"No objects written to rank {rank}")

    def verify_rank_has_no_objects(self, container, ranks):
        """Verify the excluded ranks have zero objects.

        Args:
            container (TestContainer): container to verify
            ranks (int/list): single rank or a list of ranks
        """
        if not isinstance(ranks, list):
            ranks = [ranks]
        rank_list = container.get_target_rank_lists(" after rebuild")
        objects = {
            rank: container.get_target_rank_count(rank, rank_list)
            for rank in ranks
        }
        for rank in ranks:
            self.assertEqual(objects[rank], 0, f"Excluded rank {rank} still has objects")

    def start_rebuild(self, pool, ranks):
        """Start the rebuild process.

        Args:
            pool (TestPool): pool to start rebuild on
            ranks (int/list): single rank or list of ranks to stop
        """
        # Exclude the rank from the pool to initiate rebuild
        if not isinstance(ranks, list):
            ranks = [ranks]
        self.server_managers[0].stop_ranks(ranks, force=True)

        # Wait for rebuild to start
        pool.wait_for_rebuild_to_start(1)

    def execute_during_rebuild(self, container):
        """Execute test steps during rebuild.

        Args:
            container (TestContainer): container to use
        """

    def verify_container_data(self, container, txn=None):
        """Verify the container data.

        Args:
            container (TestContainer): container to verify
            txn (int, optional): transaction timestamp to read. Defaults to None
                which uses the last timestamp written.
        """
        if container is None:
            return
        if not container.read_objects(txn):
            self.fail("Error verifying container data")

    def execute_rebuild_test(self, pool=None, container=None):
        """Execute the rebuild test steps.

        Args:
            pool (TestPool, optional): pool to use. Default is None, which will create one
            container (TestContainer, optional): container to use.
                Default is None, which will create one
        """
        # Create a pool and verify the pool information before rebuild
        if pool is None:
            pool = self.get_pool()
        self.verify_pool_info_before_rebuild(pool)

        # Create a container and write objects
        if container is None:
            ranks = self.inputs.rank.value
            container = self.create_test_container(
                pool, ranks[0] if isinstance(ranks, list) else ranks)

        # Verify the rank to be excluded has at least one object
        self.verify_rank_has_objects(container, self.inputs.rank.value)

        # Start the rebuild process
        self.start_rebuild(pool, self.inputs.rank.value)

        # Execute the test steps during rebuild
        self.execute_during_rebuild(container)

        # Confirm rebuild completes
        pool.wait_for_rebuild_to_end(1)

        # clear container status for the RF issue
        container.set_prop(prop="status", value="healthy")

        # Refresh local pool and container
        pool.check_pool_info()
        container.query()

        # Verify the excluded rank is no longer used with the objects
        self.verify_rank_has_no_objects(container, self.inputs.rank.value)

        # Verify the pool information after rebuild
        self.verify_pool_info_after_rebuild(pool)

        # Verify the container data can still be accessed
        self.verify_container_data(container)

        self.log.info("Test passed")
