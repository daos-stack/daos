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
        self.server_count = 0

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the test parameters
        self.inputs.get_params(self)

        self.server_count = len(self.hostlist_servers)

    def verify_pool_info_before_rebuild(self, pool):
        """Verify the pool information before rebuild.

        Args:
            pool (TestPool): pool to verify
        """
        info_checks = {
            "pi_uuid": pool.uuid,
            "pi_nnodes": self.server_count,
            "pi_ntargets": (
                self.server_count * self.server_managers[0].get_config_value("targets")),
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
            "pi_nnodes": self.server_count,
            "pi_ntargets": (
                self.server_count * self.server_managers[0].get_config_value("targets")),
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

    def create_test_container(self):
        """Create a container and write objects."""
        if self.container is not None:
            self.container.create()
            self.container.write_objects(
                self.inputs.rank.value, self.inputs.object_class.value)

    def verify_rank_has_objects(self, container):
        """Verify the rank to be excluded has at least one object.

        Args:
            container (TestContainer): container to verify
        """
        if container is not None:
            rank = self.inputs.rank.value
            rank_list = container.get_target_rank_lists(" before rebuild")
            qty = container.get_target_rank_count(rank, rank_list)
            self.assertGreater(
                qty, 0, "No objects written to rank {}".format(rank))

    def verify_rank_has_no_objects(self, container):
        """Verify the excluded rank has zero objects.

        Args:
            container (TestContainer): container to verify
        """
        if container is not None:
            rank = self.inputs.rank.value
            rank_list = container.get_target_rank_lists(" after rebuild")
            qty = container.get_target_rank_count(rank, rank_list)
            self.assertEqual(
                qty, 0, "Excluded rank {} still has objects".format(rank))

    def start_rebuild(self):
        """Start the rebuild process."""
        # Exclude the rank from the pool to initiate rebuild
        if isinstance(self.inputs.rank.value, list):
            self.server_managers[0].stop_ranks(self.inputs.rank.value, force=True)
        else:
            self.server_managers[0].stop_ranks([self.inputs.rank.value], force=True)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild_to_start(1)

    def execute_during_rebuild(self):
        """Execute test steps during rebuild."""

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

    def execute_rebuild_test(self, create_container=True):
        """Execute the rebuild test steps.

        Args:
            create_container (bool, optional): should the test create a
                container. Defaults to True.
        """
        # Get the test params
        self.add_pool(create=False)
        if create_container:
            self.add_container(self.pool, create=False)

        # Create a pool and verify the pool information before rebuild
        self.pool = self.get_pool()
        self.verify_pool_info_before_rebuild(self.pool)

        # Create a container and write objects
        self.create_test_container()

        # Verify the rank to be excluded has at least one object
        self.verify_rank_has_objects(self.container)

        # Start the rebuild process
        self.start_rebuild()

        # Execute the test steps during rebuild
        self.execute_during_rebuild()

        # Confirm rebuild completes
        self.pool.wait_for_rebuild_to_end(1)

        # clear container status for the RF issue
        self.container.set_prop(prop="status", value="healthy")

        # Refresh local pool and container
        self.pool.check_pool_info()
        self.container.query()

        # Verify the excluded rank is no longer used with the objects
        self.verify_rank_has_no_objects(self.container)

        # Verify the pool information after rebuild
        self.verify_pool_info_after_rebuild(self.pool)

        # Verify the container data can still be accessed
        self.verify_container_data(self.container)

        self.log.info("Test passed")
