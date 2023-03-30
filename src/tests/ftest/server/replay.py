"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers

from dfuse_utils import get_dfuse, start_dfuse, stop_dfuse, restart_dfuse
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
        """Verify data access after engine restart w/ WAL replay + w/ check pointing (DAOS-13009).

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
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
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
        ior.run(self.server_group, container.pool, container, None, ppn)

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
        ior.run(self.server_group, container.pool, container, None, ppn)

        self.log_step('Test passed')

    def test_replay_posix(self):
        """Verify POSIX data access after engine restart (DAOS-13010).

        Steps:
            0) Start 2 DAOS servers with 1 engines on each server (setup)
            1) Create a single pool and a POSIX container
            2) Start dfuse
            3) Write and then read data to the dfuse mount point
            4) After the read has completed, unmount dfuse
            5) Shutdown every engine cleanly (dmg system stop)
            6) Restart each engine (dmg system start)
            7) Remount dfuse
            8) Verify the previously written data exists
            9) Verify more data can be written

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=server,replay
        :avocado: tags=ReplayTests,test_restart_posix
        """
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
        all_ranks = self.server_managers[0].get_host_ranks(self.server_managers[0].hosts)

        self.log_step('Create a single pool and a POSIX container')
        pool = add_pool(self)
        container = self.get_container(pool)

        self.log_step('Start dfuse')
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        self.log_step('Write and then read data to the dfuse mount point')
        job_manager = get_job_manager(self, subprocess=False, timeout=60)
        ior_log = '_'.join(
            [self.test_id, pool.identifier, container.identifier, 'ior_1', 'write.log'])
        ior = get_ior(
            self, job_manager, ior_log, self.hostlist_clients, self.workdir, None,
            namespace='/run/ior_write/*')
        ior.run(self.server_group, container.pool, container, None, ppn, dfuse=dfuse)

        self.log_step('After the read has completed, unmount dfuse')
        stop_dfuse(self, dfuse, False)

        self.log_step('Shutdown every engine cleanly (dmg system stop)')
        self.get_dmg_command().system_stop(True)
        rank_check = self.server_managers[0].check_rank_state(all_ranks, ['stopped', 'excluded'], 5)
        if rank_check:
            self.log.info('Ranks %s failed to stop', rank_check)
            self.fail('Failed to stop ranks cleanly')

        self.log_step('Restart each engine (dmg system start)')
        self.get_dmg_command().system_start()
        rank_check = self.server_managers[0].check_rank_state(all_ranks, ['joined'], 5)
        if rank_check:
            self.log.info('Ranks %s failed to start', rank_check)
            self.fail('Failed to start ranks cleanly')

        self.log_step('Remount dfuse')
        restart_dfuse(self, dfuse)

        self.log_step('Verify the previously written data exists')
        ior_log = '_'.join(
            [self.test_id, pool.identifier, container.identifier, 'ior_1', 'read.log'])
        ior.update('flags', self.params.get('flags', '/run/ior_read/*'))
        ior.run(self.server_group, container.pool, container, None, ppn, dfuse=dfuse)

        self.log_step('Verify more data can be written')
        ior_log = '_'.join(
            [self.test_id, pool.identifier, container.identifier, 'ior_2', 'write.log'])
        ior = get_ior(
            self, job_manager, ior_log, self.hostlist_clients, self.workdir, None,
            namespace='/run/ior_write/*')
        ior.run(self.server_group, container.pool, container, None, ppn, dfuse=dfuse)

        self.log.info('Test passed')
