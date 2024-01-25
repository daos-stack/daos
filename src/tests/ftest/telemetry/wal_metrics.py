"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from interface_utils import CommonTI, IorTI, ServerTI, TelemetryTI
from test_utils_pool import add_pool


class WalMetrics(CommonTI, IorTI, ServerTI, TelemetryTI):
    """Tests for new specific metrics to track activity of md_on_ssd.

    :avocado: recursive
    """

    def test_wal_commit_metrics(self):
        """JIRA ID: DAOS-11626.

        The WAL commit metrics is per-engine metrics in 'dmabuff', it includes 'wal_sz', 'wal_qd'
        and 'wal_waiters' today (see dma_metrics_init() in src/bio/bio_buffer.c). WAL replay
        metrics are only updated when open a pool on engine start (or when creating a pool).

        Test steps:
        1) Verify WAL commit metrics are 0 before pool creation
        2) Create a pool
        3) Verify WAL commit metrics increase after pool creation

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=WalMetrics,test_wal_commit_metrics
        """
        wal_metrics = [item for item in self.telemetry.ENGINE_DMABUFF_METRICS if '_wal_' in item]
        verify_kwargs = {'min_value': 0, 'max_value': 0}
        verify_kwargs = {'min_value': 1}
        if not self.server_managers[0].manager.job.using_control_metadata:
            # WAL commit metrics are not expected to increase when not using MD on SSD
            verify_kwargs = {'min_value': 0, 'max_value': 0}

        self.verify_metrics('before pool creation', wal_metrics, verify_kwargs)

        self.log_step('Creating a pool (dmg pool create)')
        add_pool(self)

        self.verify_metrics('after pool creation', wal_metrics, verify_kwargs)

        self.log_step('Test passed')

    def test_wal_reply_metrics(self):
        """JIRA ID: DAOS-11626.

        The WAL replay metrics is per-pool metrics in 'vos_rehydration' under each pool folder, it
        includes 'replay_size', 'replay_time', 'replay_entries', 'replay_count' and
        'replay_transactions' (see vos_metrics_alloc() in src/vos/vos_common.c). WAL commit metrics
        are updated on each local transaction (for example, transaction for a update request, etc.)

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=WalMetrics,test_wal_reply_metrics
        """
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
        container = self.create_container()
        self.write_data(container, ppn)
        self.stop_engines()
        self.restart_engines()

        self.telemetry.get_pool_metrics()
        self.telemetry.list_metrics()
        self.log_step('Test passed')

    def test_wal_checkpoint_metrics(self):
        """JIRA ID: DAOS-11626.

        The WAL checkpoint metrics is per-pool metrics in 'checkpoint' under each pool folder, it
        includes 'duration', 'dirty_pages', 'dirty_chunks', 'iovs_copied' and 'wal_purged' (see
        vos_chkpt_metrics_init() in src/vos/vos_pool.c). WAL checkpoint metrics are update on
        checkpointing, checkpointing regularly happens in background (See the 'Checkpoint policy'
        in manual), when there is nothing to be checkpoint-ed (no new commits since last
        checkpoint), the checkpoint would be no-op and metrics won’t updated.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=WalMetrics,test_wal_checkpoint_metrics
        """
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
        frequency = 5
        container = self.create_container(
            properties=f'checkpoint:timed,checkpoint_freq:{frequency}')
        self.log.info('%s check point frequency: %s seconds', container.pool, frequency)
        self.write_data(container, ppn)

        self.log_step(f'Waiting for check pointing to complete (sleep {frequency * 2})')
        time.sleep(frequency * 2)

        self.telemetry.get_pool_metrics()
        self.telemetry.list_metrics()
        self.log_step('Test passed')
