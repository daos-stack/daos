"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from telemetry_test_base import TestWithTelemetry


class TestWithTelemetryBasic(TestWithTelemetry):
    """Test container telemetry metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)
        self.container = []
        self.metrics = {
            "open_count": {},
            "create_count": {},
            "close_count": {},
            "destroy_count": {}}
        self.pool_leader_host = None

    def create_container(self, posix):
        """Create a new container and update the metrics.

        Args:
            posix (bool): Whether or not to create a posix container
        """
        self.container.append(self.get_container(self.pool, type=("POSIX" if posix else None)))
        self.metrics["create_count"][self.pool_leader_host] += 1
        self.metrics["open_count"][self.pool_leader_host] += 1
        self.metrics["close_count"][self.pool_leader_host] += 1
        if posix:
            self.metrics["open_count"][self.pool_leader_host] += 1
            self.metrics["close_count"][self.pool_leader_host] += 1

    def open_container(self, container):
        """Open the container and update the metrics.

        Args:
            container (TestContainer): container to open
        """
        container.open()
        self.metrics["open_count"][self.pool_leader_host] += 1

    def close_container(self, container):
        """Close the container and update the metrics.

        Args:
            container (TestContainer): container to close
        """
        container.close()
        self.metrics["close_count"][self.pool_leader_host] += 1

    def destroy_container(self, container):
        """Destroy the container and update the metrics.

        Args:
            container (TestContainer): container to destroy
        """
        container.destroy()
        self.metrics["destroy_count"][self.pool_leader_host] += 1

    def check_metrics(self):
        """Check the container telemetry metrics."""
        errors = self.telemetry.check_container_metrics(**self.metrics)
        if errors:
            self.fail("\n".join(errors))

    def test_telemetry_list(self):
        """JIRA ID: DAOS-7667 / SRS-324.

        Test Description:
            Verify the dmg telemetry list command.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,telemetry
        :avocado: tags=TestWithTelemetryBasic,test_telemetry_list
        """
        self.verify_telemetry_list()

    def test_container_telemetry(self):
        """JIRA ID: DAOS-7667 / SRS-324.

        Test Description:
            Create, connect, and destroy containers to verify telemetry metrics.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,telemetry,container
        :avocado: tags=TestWithTelemetryBasic,test_container_telemetry
        """
        container_qty = self.params.get("container_qty", "/run/test/*", 1)
        open_close_qty = self.params.get("open_close_qty", "/run/test/*", 2)
        self.add_pool(connect=False)
        self.pool.set_query_data()
        pool_leader_rank = self.pool.query_data["response"]["leader"]
        self.pool_leader_host = self.server_managers[0].get_host(pool_leader_rank)
        self.log.info(
            "Pool leader host: %s (rank: %s)",
            self.pool_leader_host, pool_leader_rank)

        # Verify container telemetry metrics report 0 before container creation
        self.log.info("Before container creation")
        data = self.telemetry.get_container_metrics()
        container_metrics = self.telemetry.get_container_metrics()
        for host, data in container_metrics.items():
            self.metrics["open_count"][host] = data["engine_pool_ops_cont_open"]
            self.metrics["create_count"][host] = data["engine_pool_ops_cont_create"]
            self.metrics["close_count"][host] = data["engine_pool_ops_cont_close"]
            self.metrics["destroy_count"][host] = data["engine_pool_ops_cont_destroy"]

        # Create a number of containers and verify metrics
        for loop in range(1, container_qty + 1):
            self.create_container(self.random.choice([True, False]))
            self.log.info("Container %s/%s: After create()", loop, container_qty)
            self.check_metrics()

        # Open each container and verify metrics
        for outer_loop in range(1, open_close_qty + 1):
            # Open each container and verify metrics
            for loop, container in enumerate(self.container):
                self.open_container(container)
                self.log.info(
                    "Loop %s/%s: Container %s/%s: After open()",
                    outer_loop, open_close_qty, loop + 1, len(self.container))
                self.check_metrics()

            # Close each container and verify metrics
            for loop, container in enumerate(self.container):
                self.close_container(container)
                self.log.info(
                    "Loop %s/%s: Container %s/%s: After close()",
                    outer_loop, open_close_qty, loop + 1, len(self.container))
                self.check_metrics()

        # Destroy each container and verify metrics
        for loop, container in enumerate(self.container):
            self.destroy_container(container)
            self.log.info(
                "Container %s/%s: After destroy()",
                loop + 1, len(self.container))
            self.check_metrics()

        self.log.info("Test PASSED")
