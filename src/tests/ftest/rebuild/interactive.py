"""
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from functools import partial

from apricot import TestWithServers
from data_utils import assert_val_in_list
from exception_utils import CommandFailure
from ior_utils import get_ior
from job_manager_utils import get_job_manager


class RbldInteractive(TestWithServers):
    """Test class for interactive rebuild tests.

    :avocado: recursive
    """

    def test_rebuild_interactive(self):
        """
        Use Cases:
            Pool rebuild with interactive start/stop.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=rebuild,pool
        :avocado: tags=RbldInteractive,test_rebuild_interactive
        """
        self.log_step("Setup pool")
        pool = self.get_pool(connect=False)

        # Collect server configuration information
        server_count = len(self.hostlist_servers)
        engines_per_host = int(self.server_managers[0].get_config_value('engines_per_host') or 1)
        targets_per_engine = int(self.server_managers[0].get_config_value('targets'))
        self.log.info(
            'Running with %s servers, %s engines per server, and %s targets per engine',
            server_count, engines_per_host, targets_per_engine)

        self.log_step('Create container and run IOR')
        cont_ior = self.get_container(pool, namespace='/run/cont_ior/*')
        ior_flags_write = self.params.get('flags_write', '/run/ior/*')
        ior_ppn = self.params.get('ppn', '/run/ior/*')

        job_manager = get_job_manager(self, subprocess=False)
        ior = get_ior(
            self, job_manager, self.hostlist_clients, self.workdir, None, namespace='/run/ior/*')
        ior.manager.job.update_params(flags=ior_flags_write, dfs_oclass=cont_ior.oclass.value)
        ior.run(cont_ior.pool, cont_ior, None, ior_ppn, display_space=False)

        self.__run_rebuild_interactive(
            pool, cont_ior, ior,
            num_ranks_to_exclude=1,
            exclude_method='dmg pool exclude',
            reint_method='dmg pool reintegrate')

        self.log_step('Test Passed')

    def __run_rebuild_interactive(self, pool, cont_ior, ior,
                                  num_ranks_to_exclude, exclude_method, reint_method):
        """Run interactive rebuild test sequence.

        Args:
            pool (TestPool): pool to use
            cont_ior (TestContainer): container used for IOR
            iort (Ior): the Ior object
            num_ranks_to_exclude (int): number of ranks to exclude/reintegrate
            exclude_method (str): method to exclude ranks. Must be in
                - 'dmg pool exclude'
                - 'dmg system exclude'
            reint_method (str): method to reintegrate ranks. Must be in
                - 'dmg pool reintegrate'
                - 'dmg system reintegrate'
        """

        ior_flags_read = self.params.get('flags_read', '/run/ior/*')
        ior_ppn = self.params.get('ppn', '/run/ior/*')

        self.log_step('Verify pool state before rebuild')
        self.__verify_pool_query(
            pool, rebuild_status=0, rebuild_state=['idle', 'done'], disabled_ranks=[])

        ranks_to_exclude = self.random.sample(
            list(self.server_managers[0].ranks.keys()), k=num_ranks_to_exclude)
        self.log_step(f'Exclude random rank {ranks_to_exclude}')
        if exclude_method == 'dmg pool exclude':
            pool.exclude(ranks_to_exclude)
        elif exclude_method == 'dmg system exclude':
            pool.dmg.system_exclude(ranks_to_exclude)
        else:
            self.fail(f'Unsupported exclude_method: {exclude_method}')

        self.log_step(f'{exclude_method} - Wait for rebuild to start')
        pool.wait_for_rebuild_to_start(interval=1)

        self.log_step(f'{exclude_method} - Manually stop rebuild')
        for i in range(3):
            try:
                pool.rebuild_stop()
                break
            except CommandFailure as error:
                if i == 2 or 'DER_NONEXIST' not in str(error):
                    raise
                self.log.info('Assuming rebuild is not started yet. Retrying in 3 seconds...')
                time.sleep(3)

        self.log_step(f'{exclude_method} - Wait for rebuild to stop')
        pool.wait_for_rebuild_to_stop(interval=3)

        self.log_step(f'{exclude_method} - Verify pool state after rebuild stopped')
        self.__verify_pool_query(
            pool, rebuild_status=-2027, rebuild_state=['idle'],
            disabled_ranks=ranks_to_exclude)

        self.log_step(f'{exclude_method} - Verify IOR after rebuild stopped')
        ior.manager.job.update_params(flags=ior_flags_read)
        ior.run(cont_ior.pool, cont_ior, None, ior_ppn, display_space=False)

        self.log_step(f'{exclude_method} - Manually start rebuild')
        pool.rebuild_start()

        self.log_step(f'{exclude_method} - Wait for rebuild to start')
        pool.wait_for_rebuild_to_start(interval=1)

        self.log_step(f'{exclude_method} - Wait for rebuild to end')
        pool.wait_for_rebuild_to_end(interval=3)

        self.log_step(f'{exclude_method} - Verify pool state after rebuild completed')
        self.__verify_pool_query(
            pool, rebuild_status=0, rebuild_state=['idle', 'done'],
            disabled_ranks=ranks_to_exclude)

        self.log_step(f'{exclude_method} - Verify IOR after rebuild completed')
        ior.manager.job.update_params(flags=ior_flags_read)
        ior.run(cont_ior.pool, cont_ior, None, ior_ppn, display_space=False)

        self.log_step('Reintegrate excluded ranks')
        if reint_method == 'dmg pool reintegrate':
            pool.reintegrate(ranks_to_exclude)
        elif reint_method == 'dmg system reintegrate':
            pool.dmg.system_reintegrate(ranks_to_exclude)
        else:
            self.fail(f'Unsupported reint_method: {reint_method}')

        self.log_step(f'{reint_method} - Wait for rebuild to start')
        pool.wait_for_rebuild_to_start(interval=1)

        self.log_step(f'{reint_method} - Manually stop rebuild')
        for i in range(3):
            try:
                pool.rebuild_stop()
                break
            except CommandFailure as error:
                if i == 2 or 'DER_NONEXIST' not in str(error):
                    raise
                self.log.info('Assuming rebuild is not started yet. Retrying in 3 seconds...')
                time.sleep(3)

        self.log_step(f'{reint_method} - Wait for rebuild to stop')
        pool.wait_for_rebuild_to_stop(interval=3)

        self.log_step(f'{reint_method} - Verify pool state after rebuild stopped')
        self.__verify_pool_query(
            pool, rebuild_status=-2027, rebuild_state=['idle'],
            disabled_ranks=[])

        self.log_step(f'{reint_method} - Verify IOR after rebuild stopped')
        ior.manager.job.update_params(flags=ior_flags_read)
        ior.run(cont_ior.pool, cont_ior, None, ior_ppn, display_space=False)

        self.log_step(f'{reint_method} - Manually start rebuild')
        pool.rebuild_start()

        self.log_step(f'{reint_method} - Wait for rebuild to start')
        pool.wait_for_rebuild_to_start(interval=1)

        self.log_step(f'{reint_method} - Wait for rebuild to end')
        pool.wait_for_rebuild_to_end(interval=3)

        self.log_step(f'{reint_method} - Verify pool state after rebuild completed')
        self.__verify_pool_query(
            pool, rebuild_status=0, rebuild_state=['idle', 'done'], disabled_ranks=[])

        self.log_step(f'{reint_method} - Verify IOR after rebuild completed')
        ior.manager.job.update_params(flags=ior_flags_read)
        ior.run(cont_ior.pool, cont_ior, None, ior_ppn, display_space=False)

    def __verify_pool_query(self, pool, rebuild_status, rebuild_state, disabled_ranks):
        """Verify pool query.

        Args:
            pool (TestPool): pool to query
            rebuild_status (int): expected rebuild status
            rebuild_state (str/list): expected rebuild state
            disabled_ranks (list): expected disabled ranks

        """
        try:
            pool.verify_query(
                {
                    'rebuild': {
                        'status': rebuild_status,
                        'state': partial(assert_val_in_list, allowed_list=rebuild_state)
                    },
                    'disabled_ranks': disabled_ranks
                },
                use_cached_query=True)
        except AssertionError as error:
            self.fail(f'Unexpected pool query response: {str(error)}')
