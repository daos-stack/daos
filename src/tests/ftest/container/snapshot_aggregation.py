#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ior_test_base import IorTestBase
from daos_utils import DaosCommand


class SnapshotAggregation(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Defines snapshot aggregation test cases.

    Test Class Description:
          Verify snapshot aggregation.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a SnapshotAggregation object."""
        super().__init__(*args, **kwargs)
        self.dmg = None
        self.free_space = {"scm": [], "nvme": []}

    def update_free_space(self):
        """Append to the free space list with the current pool capacities."""
        for index, name in enumerate(("scm", "nvme")):
            for tier in self.pool.query_data["response"]["tier_stats"]:
                if tier["media_type"] == name:
                    self.free_space[name].append({
                        "dmg": tier["free"],
                        "api": int(self.pool.info.pi_space.ps_space.s_free[index])
                    })

    def test_snapshot_aggregation(self):
        """JIRA ID: DAOS-3751.

        Test Description:
            Verify snapshot aggregation with 2 servers and 6 clients (CI limit).
            Write the same data to the pool twice.  Create a snapshot between
            the writes and confirm that deleting the snapshot reduces the pool
            capacity by half.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=container,snap
        :avocado: tags=SnapshotAggregation,test_snapshot_aggregation
        """
        self.dmg = self.get_dmg_command()
        daos = DaosCommand(self.bin)

        # Create a pool and a container that spans the 2 servers.
        self.update_ior_cmd_with_pool()
        self.pool.get_info()
        self.pool.set_query_data()
        self.update_free_space()
        self.log.info(
            "Pool free space before writes:\n  SCM:  %s\n  NVMe: %s",
            self.free_space["scm"][-1], self.free_space["nvme"][-1])

        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")

        # Run an IOR job that writes >4k sequential blocks for a few minutes
        self.processes = len(self.hostlist_clients)
        manager = self.get_ior_job_manager_command()
        self.run_ior(manager, self.processes)

        # Get the capacity of the pool after running IOR.
        self.update_free_space()
        self.log.info(
            "Pool free space after first write:\n  SCM:  %s\n  NVMe: %s",
            self.free_space["scm"][-1], self.free_space["nvme"][-1])
        self.assertLess(
            self.free_space["scm"][1]["api"],
            self.free_space["scm"][0]["api"],
            "SCM free pool space was not reduced by the initial write")
        self.assertLess(
            self.free_space["nvme"][1]["api"],
            self.free_space["nvme"][0]["api"],
            "NVMe free pool space was not reduced by the initial write")

        # Create a snapshot of the container once the IOR job completes.
        self.container.create_snap()
        self.log.info("Created container snapshot: %s", self.container.epoch)

        # Run the same IOR job to cause an overwrite.
        self.ior_cmd.signature.value += 333
        self.run_ior(self.get_ior_job_manager_command(), self.processes)

        # Verify that the utilized capacity of the pool has increased.
        self.update_free_space()
        self.log.info(
            "Pool free space after second write:\n  SCM:  %s\n  NVMe: %s",
            self.free_space["scm"][-1], self.free_space["nvme"][-1])
        self.assertLess(
            self.free_space["scm"][2]["api"],
            self.free_space["scm"][1]["api"],
            "SCM free pool space was not reduced by the overwrite")
        self.assertLess(
            self.free_space["nvme"][2]["api"],
            self.free_space["nvme"][1]["api"],
            "NVMe free pool space was not reduced by the overwrite")

        # Enable the aggregation
        # Test UUID.
        self.pool.use_label = False
        self.pool.set_property("reclaim", "time")
        self.pool.use_label = True

        # Delete the snapshot.
        daos.container_destroy_snap(
            pool=self.pool.uuid,
            cont=self.container.uuid, epc=self.container.epoch)

        # Wait for aggregation to start and finish.
        space_reclaimed = False
        time_exceeded = False
        sleep_time = 20
        loop_count = 0
        while not space_reclaimed and not time_exceeded:
            loop_count += 1
            self.log.info(
                "Waiting for %s seconds for aggregation to finish - loop %s",
                sleep_time, loop_count)
            time.sleep(sleep_time)

            # Update the utilized capacity of the pool
            self.pool.get_info()
            self.pool.set_query_data()
            self.update_free_space()
            self.log.info(
                "Pool free space %s seconds after deleting the snapshot:"
                "\n  SCM:  %s\n  NVMe: %s", sleep_time * loop_count,
                self.free_space["scm"][-1], self.free_space["nvme"][-1])

            # Determine if the utilized NVMe capacity of the pool has been
            # reduced back to the capacity after the first ior write
            space_reclaimed = self.free_space["nvme"][1]["api"] == \
                self.free_space["nvme"][-1]["api"]

            # Determine if the time has exceeded
            time_exceeded = sleep_time * loop_count > 140

        if not space_reclaimed:
            self.fail(
                "Pool free space was not restored by the aggregation after "
                "snapshot deletion")

        self.log.info(
            "Pool free space restored by the aggregation after snapshot "
            "deletion")
