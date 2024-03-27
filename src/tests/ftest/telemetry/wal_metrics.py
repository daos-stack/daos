"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ior_utils import write_data
from telemetry_test_base import TestWithTelemetry
from test_utils_pool import add_pool


class WalMetrics(TestWithTelemetry):
    """Tests for new specific metrics to track activity of md_on_ssd.

    :avocado: recursive
    """

    def test_wal_commit_metrics(self):
        """JIRA ID: DAOS-11626.

        The WAL commit metrics is per-pool metrics, it includes 'wal_sz', 'wal_qd' and 'wal_waiters'
        (see vos_metrics_alloc() in src/vos/vos_common.c). WAL commit metrics are updated on each
        local transaction (for example, transaction for a update request, etc.)

        Test steps:
        1) Create a pool
        2) Verify WAL commit metrics after pool creation (non-zero w/ MD on SSD)

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=WalMetrics,test_wal_commit_metrics
        """
        wal_metrics = list(self.telemetry.ENGINE_POOL_VOS_WAL_METRICS)

        self.log_step('Creating a pool (dmg pool create)')
        add_pool(self)

        self.log_step(
            'Collect WAL commit metrics after creating a pool (dmg telemetry metrics query)')
        ranges = self.telemetry.collect_data(wal_metrics)
        for metric in list(ranges):
            if '_sz' in metric and not metric.endswith('_mean') and not metric.endswith('_stddev'):
                for label in ranges[metric]:
                    if self.server_managers[0].manager.job.using_control_metadata:
                        # The min/max/actual size should be greater than 0 for MD on SSD
                        ranges[metric][label] = [1]
                    else:
                        ranges[metric][label] = [0, 0]
            elif '_waiters' not in metric:
                ranges.pop(metric)
        if self.server_managers[0].manager.job.using_control_metadata:
            self.log_step(
                'Verify WAL commit size metrics are > 0 and waiters are 0 after creating a pool')
        else:
            self.log_step('Verify WAL commit metrics are 0 after creating a pool')
        if not self.telemetry.verify_data(ranges):
            self.fail('Unexpected WAL commit metric values after pool create')

        self.log_step('Test passed')

    def test_wal_reply_metrics(self):
        """JIRA ID: DAOS-11626.

        The WAL replay metrics is per-pool metrics in 'vos_rehydration' under each pool folder, it
        includes 'replay_size', 'replay_time', 'replay_entries', 'replay_count' and
        'replay_transactions' (see vos_metrics_alloc() in src/vos/vos_common.c). WAL replay
        metrics are only updated when open a pool on engine start (or when creating a pool).

        Test steps:
        1) Create a pool
        2) Verify WAL replay metrics after pool creation (non-zero w/ MD on SSD)

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=WalMetrics,test_wal_reply_metrics
        """
        wal_metrics = list(self.telemetry.ENGINE_POOL_VOS_WAL_REPLAY_METRICS)

        self.log_step('Creating a pool (dmg pool create)')
        add_pool(self)

        self.log_step(
            'Collect WAL replay metrics after creating a pool (dmg telemetry metrics query)')
        ranges = self.telemetry.collect_data(wal_metrics)
        for metric in sorted(ranges):
            for label in ranges[metric]:
                if self.server_managers[0].manager.job.using_control_metadata:
                    if metric.endswith('_replay_count'):
                        # Replay count should be 1 after pool create for MD on SSD
                        ranges[metric][label] = [1, 1]
                    elif metric.endswith('_replay_entries'):
                        # Replay entries should be > 0 after pool create for MD on SSD
                        ranges[metric][label] = [1]
                    elif metric.endswith('_replay_size'):
                        # Replay size should be > 0 after pool create for MD on SSD
                        ranges[metric][label] = [1]
                    elif metric.endswith('_replay_time'):
                        # Replay time should be 10,000 - 50,000 after pool create for MD on SSD
                        ranges[metric][label] = [10000, 50000]
                    elif metric.endswith('_replay_transactions'):
                        # Replay transactions should be > 0 after pool create for MD on SSD
                        ranges[metric][label] = [1]
                else:
                    ranges[metric][label] = [0, 0]

        self.log_step('Verify WAL reply metrics after pool creation (dmg telemetry metrics query)')
        if not self.telemetry.verify_data(ranges):
            self.fail('WAL replay metrics verification failed after pool creation')

        self.log_step('Test passed')

    def test_wal_checkpoint_metrics(self):
        """JIRA ID: DAOS-11626.

        The WAL checkpoint metrics is per-pool metrics in 'checkpoint' under each pool folder, it
        includes 'duration', 'dirty_pages', 'dirty_chunks', 'iovs_copied' and 'wal_purged' (see
        vos_chkpt_metrics_init() in src/vos/vos_pool.c). WAL checkpoint metrics are update on
        check pointing, check pointing regularly happens in background (See the 'Checkpoint policy'
        in manual), when there is nothing to be checkpoint-ed (no new commits since last
        checkpoint), the checkpoint would be no-op and metrics won’t updated.

        Test steps:
        1) Create a pool w/o check pointing
        2) Verify WAL checkpoint metrics are zero after pool creation
        3) Create a second pool w/ check pointing enabled
        4) Verify WAL checkpoint metrics are zero for both pools after pool creation
        5) Write some data to a container in the second pool
        6) Wait enough time for check pointing to have occurred
        7) Verify WAL checkpoint purged metrics are non-zero for the second pool (for MD on SSD)

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=WalMetrics,test_wal_checkpoint_metrics
        """
        frequency = 5
        wal_metrics = list(self.telemetry.ENGINE_POOL_CHECKPOINT_METRICS)

        self.log_step('Creating a pool with check pointing disabled (dmg pool create)')
        add_pool(self, properties='checkpoint:disabled')

        self.log_step(
            'Collect WAL checkpoint metrics after creating a pool w/o check pointing '
            '(dmg telemetry metrics query)')
        ranges = self.telemetry.collect_data(wal_metrics)
        for metric, values in ranges.items():
            for label in values:
                # Initially all metrics should be 0
                values[label] = [0, 0]

        self.log_step(
            'Verifying WAL checkpoint metrics are all 0 after creating a pool w/o check pointing')
        if not self.telemetry.verify_data(ranges):
            self.fail('WAL check point metrics not zero after creating a pool w/o check pointing')

        self.log_step('Creating a pool with timed check pointing (dmg pool create)')
        pool = add_pool(self, properties=f'checkpoint:timed,checkpoint_freq:{frequency}')

        self.log_step(
            'Collect WAL checkpoint metrics after creating a pool w/ check pointing '
            '(dmg telemetry metrics query)')
        ranges = self.telemetry.collect_data(wal_metrics)
        for metric, values in ranges.items():
            for label in values:
                uuid = pool.uuid
                if uuid in label and self.server_managers[0].manager.job.using_control_metadata:
                    if '_dirty_chunks' in metric:
                        # Check point dirty chunks should be 0-300 after pool create for MD on SSD
                        values[label] = [0, 300]
                    elif '_dirty_pages' in metric:
                        # Check point dirty pages should be 0-3 after pool create for MD on SSD
                        values[label] = [0, 3]
                    elif '_duration' in metric:
                        # Check point duration should be 0-1,000,000 after pool create for MD on SSD
                        values[label] = [0, 1000000]
                    elif '_iovs_copied' in metric:
                        # Check point iovs copied should be >= 0 after pool create for MD on SSD
                        values[label] = [0]
                    elif '_wal_purged' in metric:
                        # Check point wal purged should be >= 0 after pool create for MD on SSD
                        values[label] = [0]
                else:
                    # All metrics for the pool w/o check pointing or w/o MD on SSD should be 0
                    values[label] = [0, 0]
        self.log_step('Verifying WAL check point metrics after creating a pool w/ check pointing')
        if not self.telemetry.verify_data(ranges):
            self.fail('WAL replay metrics verification failed after pool w/ check pointing create')

        self.log_step('Creating a container for the pool w/ check pointing (daos container create)')
        container = self.get_container(pool)
        self.log.info('%s check point frequency: %s seconds', container.pool, frequency)

        self.log_step('Writing data to the pool w/ check pointing (ior)')
        write_data(self, container)

        self.log_step(f'Waiting for check pointing to complete (sleep {frequency * 2})')
        time.sleep(frequency * 2)

        self.log_step('Collect WAL checkpoint metrics after check pointing is complete')
        self.telemetry.collect_data(wal_metrics)
        if self.server_managers[0].manager.job.using_control_metadata:
            for metric, values in ranges.items():
                for label in values:
                    if pool.uuid in label:
                        if '_wal_purged' in metric:
                            # Check point wal purged should be > 0 after check point for MD on SSD
                            values[label] = [1]
        self.log_step(
            'Verify WAL checkpoint metrics after check pointing is complete '
            '(dmg telemetry metrics query)')
        if not self.telemetry.verify_data(ranges):
            self.fail('WAL replay metrics verification failed after check pointing completion')

        self.log_step('Test passed')
