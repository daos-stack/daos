#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import error
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
        self.metrics = {
            "open_count": 0,
            "active_count": 0,
            "close_count": 0,
            "destroy_count": 0
        }

    def create_container(self):
        """Create a new container and update the metrics."""
        self.container.append(self.get_container(self.pool))
        self.metrics["open_count"] += 1
        self.metrics["close_count"] += 1

    def open_container(self, container):
        """Open the container and update the metrics.

        Args:
            container (TestContainer): container to open
        """
        container.open()
        self.metrics["open_count"] += 1
        self.metrics["active_count"] += 1

    def close_container(self, container):
        """Close the container and update the metrics.

        Args:
            container (TestContainer): container to close
        """
        container.close()
        self.metrics["close_count"] += 1
        self.metrics["active_count"] -= 1

    def destroy_container(self, container):
        """Destroy the container and update the metrics.

        Args:
            container (TestContainer): container to destroy
        """
        container.destroy()
        self.metrics["destroy_count"] += 1

    def check_metrics(self, telemetry):
        """Check the container telemetry metrics.

        Args:
            telemetry (TelemetryUtils): TelemetryUtils object
        """
        errors = telemetry.check_container_metrics(**self.metrics)
        if errors:
            for error in errors:
                self.log.error("MERTIC_ERROR: %s", error)
            # self.fail("\n".join(errors))

    def test_telemetry_list(self):
        """JIRA ID: DAOS-7667 / SRS-324.

        Test Description:
            Verify the dmg telemetry list command.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,telemetry
        :avocado: tags=test_with_telemetry,test_telemetry_list
        """
        metric_list_qty = self.params.get("metric_list_qty", "/run/test/*", 1)
        telemetry = TelemetryUtils(
            self.get_dmg_command(), self.server_managers[0].hosts)

        # List all of the telemetry metrics
        result = telemetry.list_metrics()

        # Verify the list
        errors = []
        self.log.info(
            "Verifying telemetry metrics list for %s/%s hosts:",
            len(result), len(self.server_managers[0].hosts))
        if sorted(result) != sorted(self.server_managers[0].hosts):
            msg = "Telemetry metrics names missing for hosts {}: {}".format(
                self.server_managers[0].hosts, result)
            self.log.error("  - %s", msg)
            errors.append(msg)
        for host in result:
            self.log.info(
                "  %s: detected %s/%s telemetry metric names",
                host, len(result[host]), metric_list_qty)
            if len(result[host]) != metric_list_qty:
                msg = "Missing {} telemetry metrics for {}".format(
                    metric_list_qty - len(result[host]), host)
                self.log.error("    - %s", msg)
                errors.append(msg)
        if errors:
            self.fail("\n".join(errors))

    def test_container_telemetry(self):
        """JIRA ID: DAOS-7667 / SRS-324.

        Test Description:
            Create, connect, and destroy containers to verify telemetry metrics.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,telemetry,container
        :avocado: tags=test_with_telemetry,test_container_telemetry
        """
        container_qty = self.params.get("container_qty", "/run/test/*", 1)
        open_close_qty = self.params.get("open_close_qty", "/run/test/*", 2)
        telemetry = TelemetryUtils(
            self.get_dmg_command(), self.server_managers[0].hosts)
        self.add_pool(connect=False)

        # Verify container telemetry metrics report 0 before container creation
        self.log.info("Before container creation")
        data = telemetry.get_container_metrics()
        for host in data:
            self.metrics["open_count"] = \
                data[host]["engine_container_ops_open_total"]
            self.metrics["active_count"] = \
                data[host]["engine_container_ops_open_active"]
            self.metrics["close_count"] = \
                data[host]["engine_container_ops_close_total"]
            self.metrics["destroy_count"] = \
                data[host]["engine_container_ops_destroy_total"]

        # Create a number of containers and verify metrics
        for loop in range(1, container_qty + 1):
            self.create_container()
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
                self.close_container(container)
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
