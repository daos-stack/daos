"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from general_utils import join
from ior_utils import get_ior
from job_manager_utils import get_job_manager
from telemetry_test_base import TestWithTelemetry
from test_utils_pool import add_pool


class CommonTI(TestWithServers):
    """Common Test Interface."""

    def create_container(self, details=None, **pool_params):
        """Create a pool and container.

        Args:
            test (Test): avocado Test object
            details (str, optional): additional log_step messaging
            pool_params (dict, optional): named arguments to add_pool()

        Returns:
            TestContainer: the created container with a reference to the created pool
        """
        if details is not None:
            details = join(' ', '-', details)
        self.log_step(join(' ', 'Creating a pool (dmg pool create)', details))
        pool = add_pool(self, **pool_params)
        self.log_step(join(' ', 'Creating a container (daos container create)', details))
        return self.get_container(pool)


class IorTI(TestWithServers):
    """Ior Test Interface."""

    def write_data(self, container, ppn, dfuse=None, namespace='/run/ior_write/*'):
        """Write data to the container/dfuse using ior.

        Args:
            container (TestContainer): the container to populate
            ppn (int): processes per node to use with the ior command
            dfuse (Dfuse, optional): dfuse object defining the dfuse mount point. Defaults to None.
            namespace (str, optional): path to ior yaml parameters. Defaults to '/run/ior_write/*'.

        Returns:
            Ior: the Ior object used to populate the container
        """
        self.log_step('Write data to the container (ior)')
        job_manager = get_job_manager(self, subprocess=False, timeout=60)
        ior = get_ior(self, job_manager, self.hostlist_clients, self.workdir, None, namespace)
        ior.run(self.server_group, container.pool, container, None, ppn, dfuse=dfuse)
        return ior

    def read_data(self, ior, container, ppn, dfuse=None, namespace='/run/ior_read/*'):
        """Verify the data used to populate the container.

        Args:
            ior (Ior): the ior command used to populate the container
            container (TestContainer): the container to verify
            ppn (int): processes per node to use with the ior command
            dfuse (Dfuse, optional): dfuse object defining the dfuse mount point. Defaults to None.
            namespace (str, optional): path to ior yaml parameters. Defaults to '/run/ior_read/*'.
        """
        self.log_step('Read data from the container (ior)')
        ior.update('flags', self.params.get('flags', namespace))
        ior.run(self.server_group, container.pool, container, None, ppn, dfuse=dfuse)


class ServerTI(TestWithServers):
    """Server Test Interface."""

    def stop_engines(self):
        """Stop each server engine and verify they are not running."""
        self.log_step('Shutting down the engines (dmg system stop)')
        self.get_dmg_command().system_stop(True)

        # Verify all ranks have stopped
        all_ranks = self.server_managers[0].get_host_ranks(self.server_managers[0].hosts)
        rank_check = self.server_managers[0].check_rank_state(all_ranks, ['stopped', 'excluded'], 5)
        if rank_check:
            self.log.info('Ranks %s failed to stop', rank_check)
            self.fail('Failed to stop ranks cleanly')

    def restart_engines(self):
        """Restart each server engine and verify they are running."""
        self.log_step('Restarting the engines (dmg system start)')
        self.get_dmg_command().system_start()

        # Verify all ranks have started
        all_ranks = self.server_managers[0].get_host_ranks(self.server_managers[0].hosts)
        rank_check = self.server_managers[0].check_rank_state(all_ranks, ['joined'], 5)
        if rank_check:
            self.log.info('Ranks %s failed to start', rank_check)
            self.fail('Failed to start ranks cleanly')


class SnapshotTI(TestWithServers):
    """Snapshot Test Interface."""

    def verify_snapshots(self, container, expected):
        """Verify the snapshots listed for the container match the expected list of snapshots.

        Args:
            container (TestContainer): the container from which to get the detected snapshots
            expected (list): the expected lists of snapshots

        Raises:
            TestFail: if the detected list of snapshots does not match the detected list
        """
        self.log.debug("Expected list of snapshots: %s", expected)
        detected = [entry["epoch"] for entry in container.list_snaps()["response"]]
        self.assertListEqual(
            sorted(expected), sorted(detected), 'Detected snapshots does not match expected')


class TelemetryTI(TestWithTelemetry):
    """Telemetry Test Interface."""

    def verify_metrics(self, details, metrics, kwargs):
        """Collect and verify telemetry metrics data.

        Args:
            details (str): description of the telemetry data
            metrics (list): list of metric names to collect and verify
            kwargs (dict): optional 'min_value' and 'max_value' arguments for verify_metric_value()
        """
        self.log_step(join(' ', 'Verify metric values', details, '(dmg telemetry metrics query)'))
        kwargs['metrics_data'] = self.telemetry.get_nvme_metrics(metrics)
        if not self.telemetry.verify_metric_value(**kwargs):
            self.fail(join(' ', 'Unexpected WAL commit metric values', details))
