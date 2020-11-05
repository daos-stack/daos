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
from apricot import TestWithServers
from command_utils_base import ObjectWithParameters, BasicParameter
from test_utils_pool import TestPool
from test_utils_container import TestContainer


class RebuildTestParams(ObjectWithParameters):
    """Class for gathering test parameters."""

    def __init__(self):
        """Initialize a RebuildTestParams object."""
        super(RebuildTestParams, self).__init__("/run/rebuild/*")
        self.object_class = BasicParameter(None)
        self.rank = BasicParameter(None)


class RebuildTestBase(TestWithServers):
    """Base rebuild test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a RebuildTestBase object."""
        super(RebuildTestBase, self).__init__(*args, **kwargs)
        self.inputs = RebuildTestParams()
        self.targets = None
        self.server_count = 0
        self.info_checks = None
        self.rebuild_checks = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(RebuildTestBase, self).setUp()

        # Get the test parameters
        self.inputs.get_params(self)

        # Get the number of targets per server for pool info calculations
        self.targets = self.params.get("targets", "/run/server_config/*")

        self.server_count = len(self.hostlist_servers)

    def setup_test_pool(self):
        """Define a TestPool object."""
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)

    def setup_test_container(self):
        """Define a TestContainer object."""
        self.container = TestContainer(self.pool)
        self.container.get_params(self)

    def setup_pool_verify(self):
        """Set up pool verification initial expected values."""
        self.info_checks = {
            "pi_uuid": self.pool.uuid,
            "pi_nnodes": self.server_count,
            "pi_ntargets": (self.server_count * self.targets),
            "pi_ndisabled": 0,
        }
        self.rebuild_checks = {
            "rs_done": 1,
            "rs_obj_nr": 0,
            "rs_rec_nr": 0,
            "rs_errno": 0,
        }

    def update_pool_verify(self):
        """Update the pool verification expected values."""
        self.info_checks["pi_ndisabled"] = ">0"
        self.rebuild_checks["rs_obj_nr"] = ">0"
        self.rebuild_checks["rs_rec_nr"] = ">0"

    def execute_pool_verify(self, msg=None):
        """Verify the pool info.

        Args:
            msg (str, optional): additional information to include in the error
                message. Defaults to None.
        """
        status = self.pool.check_pool_info(**self.info_checks)
        status &= self.pool.check_rebuild_status(**self.rebuild_checks)
        self.assertTrue(
            status,
            "Error confirming pool info{}".format("" if msg is None else msg))

    def create_test_pool(self):
        """Create the pool and verify its info."""
        # Create a pool
        self.pool.create()

        # Verify the pool information before rebuild
        self.setup_pool_verify()
        self.execute_pool_verify(" before rebuild")

    def create_test_container(self):
        """Create a container and write objects."""
        if self.container is not None:
            self.container.create()
            self.container.write_objects(
                self.inputs.rank.value, self.inputs.object_class.value)

    def verify_rank_has_objects(self):
        """Verify the rank to be excluded has at least one object."""
        if self.container is not None:
            rank = self.inputs.rank.value
            rank_list = self.container.get_target_rank_lists(" before rebuild")
            qty = self.container.get_target_rank_count(rank, rank_list)
            self.assertGreater(
                qty, 0, "No objects written to rank {}".format(rank))

    def verify_rank_has_no_objects(self):
        """Verify the excluded rank has zero objects."""
        if self.container is not None:
            rank = self.inputs.rank.value
            rank_list = self.container.get_target_rank_lists(" after rebuild")
            qty = self.container.get_target_rank_count(rank, rank_list)
            self.assertEqual(
                qty, 0, "Excluded rank {} still has objects".format(rank))

    def start_rebuild(self):
        """Start the rebuild process."""
        # Exclude the rank from the pool to initiate rebuild
        if isinstance(self.inputs.rank.value, list):
            self.pool.start_rebuild(self.inputs.rank.value, self.d_log)
        else:
            self.pool.start_rebuild([self.inputs.rank.value], self.d_log)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True, 1)

    def execute_during_rebuild(self):
        """Execute test steps during rebuild."""
        pass

    def verify_container_data(self, txn=None):
        """Verify the container data.

        Args:
            txn (int, optional): transaction timestamp to read. Defaults to None
                which uses the last timestamp written.
        """
        if self.container is not None:
            self.assertTrue(
                self.container.read_objects(txn),
                "Error verifying container data")

    def execute_rebuild_test(self, create_container=True):
        """Execute the rebuild test steps.

        Args:
            create_container (bool, optional): should the test create a
                container. Defaults to True.
        """
        # Get the test params
        self.setup_test_pool()
        if create_container:
            self.setup_test_container()

        # Create a pool and verify the pool information before rebuild
        self.create_test_pool()

        # Create a container and write objects
        self.create_test_container()

        # Verify the rank to be excluded has at least one object
        self.verify_rank_has_objects()

        # Start the rebuild process
        self.start_rebuild()

        # Execute the test steps during rebuild
        self.execute_during_rebuild()

        # Confirm rebuild completes
        self.pool.wait_for_rebuild(False, 1)

        # Verify the excluded rank is no longer used with the objects
        self.verify_rank_has_no_objects()

        # Verify the pool information after rebuild
        self.update_pool_verify()
        self.execute_pool_verify(" after rebuild")

        # Verify the container data can still be accessed
        self.verify_container_data()

        self.log.info("Test passed")
