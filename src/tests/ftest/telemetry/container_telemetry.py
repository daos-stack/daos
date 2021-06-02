#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from telemetry_utils import TelemetryUtils


class TestWithTelemetry(TestWithServers):
    """Test container telemetry metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)
        self.container = []
        self.metrics = {"open": 0, "active": 0, "close": 0, "destroy": 0}

    def open_container(self, container):
        """Open the container and update the metrics.

        Args:
            container (TestContainer): container to open
        """
        container.open()
        self.metrics["open"] += 1
        self.metrics["active"] += 1

    def close_container(self, container):
        """Close the container and update the metrics.

        Args:
            container (TestContainer): container to close
        """
        container.open()
        self.metrics["close"] += 1
        self.metrics["active"] -= 1

    def destroy_container(self, container):
        """Destroy the container and update the metrics.

        Args:
            container (TestContainer): container to destroy
        """
        container.destroy()
        self.metrics["destroy"] += 1

    def check_metrics(self, telemetry):
        """Check the container telemetry metrics.

        Args:
            telemetry (TelemetryUtils): TelemetryUtils object
        """
        errors = telemetry.check_container_metrics(**self.metrics)
        if errors:
            self.fail("\n".join(errors))

    def test_container_telemetry(self):
        """JIRA ID: DAOS-7667 / SRS-324.

        Test Description:
            Create, connect, and destroy containers to verify telemetry metrics.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=telemetry,container
        :avocado: tags=test_with_telemetry,test_container_telemetry
        """
        container_qty = self.params.get("container_qty", "/run/test/*", 1)
        open_close_qty = self.params.get("open_close_qty", "/run/test/*", 2)
        telemetry = TelemetryUtils(
            self.get_dmg_command(), self.server_managers[0].hosts)
        self.add_pool(connect=False)

        # Verify container telemetry metrics report 0 before container creation
        self.log.info("Before container creation")
        self.check_metrics(telemetry)

        # Create a number of containers and verify metrics
        for loop in range(1, container_qty + 1):
            self.container.append(self.get_container(self.pool))
            self.log.info(
                "Container %s/%s: After create()", loop, container_qty + 1)
            self.check_metrics(telemetry)

        # Open each container and verify metrics
        for outer_loop in range(1, open_close_qty + 1):
            # Open each container and verify metrics
            for loop, container in enumerate(self.container):
                self.open_container(container)
                self.log.info(
                    "Loop %s/%s: Container %s/%s: After open()",
                    outer_loop, open_close_qty + 1, loop, len(self.container))
                self.check_metrics(telemetry)

            # Close each container and verify metrics
            for loop, container in enumerate(self.container):
                self.close_container()
                self.log.info(
                    "Loop %s/%s: Container %s/%s: After close()",
                    outer_loop, open_close_qty + 1, loop, len(self.container))
                self.check_metrics(telemetry)

        # Destroy each container
        for loop, container in enumerate(self.container):
            self.destroy_container(container)
            self.log.info(
                "Container %s/%s: After destroy()", loop, len(self.container))
            self.check_metrics(telemetry)
