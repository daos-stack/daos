"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from ior_utils import get_ior
from job_manager_utils import get_job_manager
from test_utils_pool import add_pool


class ReplayTests(TestWithServers):
    """Shutdown/restart/replay test cases.

    Restarting engines with volatile SCM will include loading the blob from the SSD and re-applying
    any changes from the WAL.

    :avocado: recursive
    """

    def test_restart(self):
        """Verify data access after engine restart w/ WAL replay + w/ check pointing.

        Tests un-synchronized WAL & VOS

        Steps:
            0) Start 2 DAOS servers with 1 engines on each server (setup)
            1) Create a single pool and container
            2) Run ior w/ DFS to populate the container with data
            3) After ior has completed, shutdown every engine cleanly (dmg system stop)
            4) Remove VOS file manually/temporarily (umount tmpfs; remount tmpfs)
            5) Restart each engine (dmg system start)
            6) Verify the previously written data matches with an ior read

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=server,replay
        :avocado: tags=ReplayTests,test_restart
        """
        processes = self.params.get('ppn', '/run/ior_write/*', 1)
        self.log_step('Creating a pool and container')
        pool = add_pool(self)
        container = self.get_container(pool)

        self.log_step('Populating the container with data via ior')
        job_manager = get_job_manager(self, subprocess=False, timeout=60)
        ior_log = '_'.join(
            [self.test_id, pool.identifier, container.identifier, 'ior', 'write.log'])
        ior = get_ior(
            self, job_manager, ior_log, self.hostlist_clients, self.workdir, None,
            namespace='/run/ior_write/*')
        ior.run(self.server_group, container.pool, container, processes)

        self.log_step('Shutting down the engines')
        self.get_dmg_command().system_stop(True)

        # Verify all ranks have stopped
        all_ranks = self.server_managers[0].get_host_ranks(self.server_managers[0].hosts)
        rank_check = self.server_managers[0].check_rank_state(all_ranks, ['stopped', 'excluded'], 5)
        if rank_check:
            self.log.info('Ranks %s failed to stop', rank_check)
            self.fail('Failed to stop ranks cleanly')

        self.log_step('Restarting the engines')
        self.get_dmg_command().system_start()

        # Verify all ranks have started
        all_ranks = self.server_managers[0].get_host_ranks(self.server_managers[0].hosts)
        rank_check = self.server_managers[0].check_rank_state(all_ranks, ['joined'], 5)
        if rank_check:
            self.log.info('Ranks %s failed to start', rank_check)
            self.fail('Failed to start ranks cleanly')

        self.log_step('Verifying the container data previous written via ior')
        ior_log = '_'.join([self.test_id, pool.identifier, container.identifier, 'ior', 'read.log'])
        ior.update('flags', self.params.get('flags', '/run/ior_read/*'))
        ior.run(self.server_group, container.pool, container, processes)

        self.log_step('Test passed')
