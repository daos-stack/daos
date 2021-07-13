#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from rebuild_test_base import RebuildTestBase
from daos_utils import DaosCommand
import re

class RbldContRedundancyFactor(RebuildTestBase):
    # pylint: disable=too-many-ancestors
    """Test cascading failures during rebuild.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CascadingFailures object."""
        super().__init__(*args, **kwargs)
        self.mode = None
        self.daos_cmd = None

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
            "#No objects written to rank {}".format(self.inputs.rank.value[0]))

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
                "#Excluded rank {} still has objects".format(rank))

    def verify_cont_rf_healthstatus(self, expected_rf, expected_health):
        """Verify the container redundancy factor and health status."""
        result = self.daos_cmd.container_get_prop(
                      pool=self.pool.uuid,
                      cont=self.container.uuid)
        rf = re.search(r"Redundancy Factor\s*([A-Za-z0-9-]+)",
                       str(result.stdout)).group(1)
        health = re.search(r"Health\s*([A-Z]+)", str(result.stdout)).group(1)
        self.assertEqual(
            rf, expected_rf,
            "#Container redundancy factor mismatch, actual: {},"
            " expected: {}.".format(rf, expected_rf))
        self.assertEqual(
            health, expected_health,
            "#Container health-status mismatch, actual: {},"
            " expected: {}.".format(health, expected_health))

    def start_rebuild(self):
        """Start the rebuild process and check for container properties."""
        self.log.info(
            "==>(1)Check for container rf and health-status "
            "before rebuild: HEALTHY")
        rf = ''.join(self.container.properties.value.split(":"))
        self.verify_cont_rf_healthstatus(rf, "HEALTHY")

        # Exclude the first rank from the pool to initiate rebuild
        self.server_managers[0].stop_ranks(
            [self.inputs.rank.value[0]], self.d_log)
        self.log.info(
            "==>(2)Check for container rf and health-status "
            "after 1st rank rebuild: HEALTHY")
        self.verify_cont_rf_healthstatus(rf, "HEALTHY")
        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True, 1)

    def execute_during_rebuild(self):
        """Execute test steps during rebuild."""
        self.daos_cmd = DaosCommand(self.bin)
        rf = ''.join(self.container.properties.value.split(":"))
        # Exclude the second rank from the pool during rebuild
        self.server_managers[0].stop_ranks(
            [self.inputs.rank.value[1]], self.d_log)

        self.log.info(
            "==>(3)Check for container rf and health-status "
            "after the 2nd rank rebuild: HEALTHY")
        self.verify_cont_rf_healthstatus(rf, "HEALTHY")
        self.server_managers[0].stop_ranks(
            [self.inputs.rank.value[2]], self.d_log)

        self.log.info(
            "==>(4)Check for container rf and health-status "
            "after the 3rd rank rebuild: UNCLEAN")
        self.verify_cont_rf_healthstatus(rf, "UNCLEAN")

        # Wait for rebuild completion and set container property healthy
        self.pool.wait_for_rebuild(False, 1)
        self.daos_cmd.container_set_prop(
                      pool=self.pool.uuid,
                      cont=self.container.uuid,
                      prop="status",
                      value="healthy")
        self.log.info(
            "==>(5)Check for container rf and health-status "
            "after rebuild completed: HEALTHY")
        self.verify_cont_rf_healthstatus(rf, "HEALTHY")

        # Populate the container with additional data after rebuild
        self.container.write_objects(obj_class=self.inputs.object_class.value)

    def test_container_redundancy_factor_with_rebuild(self):
        """Jira ID:
        DAOS-6270: container with RF 2 can lose up to 2 concurrent
                   servers without err.
        DAOS-6271: container with RF 2 error reported after more than
                   2 concurrent servers failure occurred
        Description:
            Test step:
                (0)Create pool and container with redundant factor
                   Start background IO object write, and server-rank
                   system stop sequentially.
                (1)Check for container initial rf and health-status.
                (2)Check for container rf and health-status after the
                   1st rank rebuild.
                (3)Check for container rf and health-status after the
                   2nd rank rebuild.
                (4)Check for container rf and health-status after the
                   3rd rank rebuild.
                (5)Check for container rf and health-status after the
                   rebuild completed.
                (6)Verify container io object write.
        Use Cases:
            Verify container RF with rebuild with multiple server failures.

        :avocado: tags=all,full_regression
        :avocado: tags=container,rebuild
	:avocado: tags=container_redundancy
        """
        self.mode = "container_rf"
        self.execute_rebuild_test()
