"""
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading
import time
from functools import partial
from multiprocessing import Queue

from apricot import TestWithServers
from avocado.utils.process import CmdResult
from data_utils import assert_val_in_list
from ior_utils import get_ior, thread_run_ior
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
        self.log_step('Setup first pool with half available space')
        pool1 = self.get_pool(size='50%', connect=False)

        # Collect server configuration information
        server_count = len(self.hostlist_servers)
        engines_per_host = int(self.server_managers[0].get_config_value('engines_per_host') or 1)
        targets_per_engine = int(self.server_managers[0].get_config_value('targets'))
        self.log.info(
            'Running with %s servers, %s engines per server, and %s targets per engine',
            server_count, engines_per_host, targets_per_engine)

        self.log_step('Create container and run IOR')
        cont1 = self.get_container(pool1)
        ior_flags_write = self.params.get('flags_write', '/run/ior/*')
        ior_flags_read = self.params.get('flags_read', '/run/ior/*')
        ior_ppn = self.params.get('ppn', '/run/ior/*')

        ior1 = get_ior(
            self, get_job_manager(self, subprocess=False), self.hostlist_clients,
            self.workdir, None, namespace='/run/ior/*')
        ior1.manager.job.update_params(
            flags=ior_flags_write, dfs_oclass=cont1.oclass.value,
            dfs_pool=pool1.identifier, dfs_cont=cont1.identifier)
        ior1.run(ppn=ior_ppn, display_space=False)

        # Update ior with read flags for verification later
        ior1.manager.job.update_params(flags=ior_flags_read)

        self.log_step('Start IOR in the background')
        cont_background = self.get_container(pool1)
        thread_queue = Queue()
        ior_background_namespace = "/run/ior_background/*"
        ior_kwargs = {
            "thread_queue": thread_queue,
            "job_id": 0,
            "test": self,
            "manager": get_job_manager(self, subprocess=False),
            "log": "ior_thread.log",
            "hosts": self.hostlist_clients,
            "path": self.workdir,
            "slots": None,
            "pool": pool1,
            "container": cont_background,
            "processes": self.params.get("np", ior_background_namespace),
            "ppn": self.params.get("ppn", ior_background_namespace),
            "display_space": False,
            "namespace": ior_background_namespace,
            "ior_params": {
                "dfs_oclass": cont_background.oclass.value
            }
        }
        ior_thread = threading.Thread(target=thread_run_ior, kwargs=ior_kwargs)
        ior_thread.start()
        if not ior_thread.is_alive():
            self.fail("Background IOR thread failed to start")

        rebuild_sequence_start = time.time()
        self.__run_rebuild_interactive(
            [pool1], [ior1],
            num_ranks_to_exclude=1,
            exclude_method='dmg pool exclude',
            reint_method='dmg pool reintegrate',
            stop_method='dmg pool rebuild stop',
            start_method='dmg pool rebuild start')
        rebuild_sequence_duration = time.time() - rebuild_sequence_start
        self.log.info("Rebuild sequence completed in %.2f seconds", rebuild_sequence_duration)

        self.log_step("Wait for background IOR to finish")
        ior_thread.join()
        if thread_queue.empty():
            self.fail("Did not receive a result from background IOR")
        ior_result = thread_queue.get()
        if not isinstance(ior_result["result"], CmdResult):
            self.fail(f"Background IOR failed: {ior_result['result']}")
        self.log.debug("Result from background IOR:")
        for name in ("command", "exit_status", "interrupted", "duration"):
            self.log.debug("  %s: %s", name, getattr(ior_result["result"], name))
        for name in ("stdout", "stderr"):
            self.log.debug("  %s:", name)
            for line in getattr(ior_result["result"], name).splitlines():
                self.log.debug("    %s", line)
        if ior_result["result"].exit_status != 0:
            self.fail("Background IOR failed")
        ior_thread_duration = ior_result["result"].duration
        self.log.info("Background IOR completed in %.2f seconds", ior_thread_duration)
        if ior_thread_duration < rebuild_sequence_duration:
            self.fail(
                "Background IOR completed before rebuild sequence. "
                "Need to increase background IOR runtime or iterations.")

        self.log_step('Setup second pool with all remaining space')
        pool2 = self.get_pool(size='100%', connect=False)

        self.log_step('Create second container and run IOR')
        cont2 = self.get_container(pool2)

        ior2 = get_ior(
            self, get_job_manager(self, subprocess=False), self.hostlist_clients,
            self.workdir, None, namespace='/run/ior/*')
        ior2.manager.job.update_params(
            flags=ior_flags_write, dfs_oclass=cont2.oclass.value,
            dfs_pool=pool2.identifier, dfs_cont=cont2.identifier)
        ior2.run(ppn=ior_ppn, display_space=False)

        # Update ior with read flags for verification later
        ior2.manager.job.update_params(flags=ior_flags_read)

        self.__run_rebuild_interactive(
            [pool1, pool2], [ior1, ior2],
            num_ranks_to_exclude=1,
            exclude_method='dmg system exclude',
            reint_method='dmg system reintegrate',
            stop_method='dmg system rebuild stop',
            start_method='dmg system rebuild start')

        self.log_step('Test Passed')

    def __run_rebuild_interactive(self, pools, iors,
                                  num_ranks_to_exclude, exclude_method, reint_method,
                                  stop_method, start_method):
        # pylint: disable=too-many-branches
        """Run interactive rebuild test sequence.


        Args:
            pools (list): list of TestPool to use
            iors (list): list of Ior objects to verify data consistency
            num_ranks_to_exclude (int): number of ranks to exclude/reintegrate
            exclude_method (str): method to exclude ranks. Must be in
                - 'dmg pool exclude'
                - 'dmg system exclude'
            reint_method (str): method to reintegrate ranks. Must be in
                - 'dmg pool reintegrate'
                - 'dmg system reintegrate'
            stop_method (str): method to stop rebuild with. Must be in
                - 'dmg pool rebuild stop'
                - 'dmg system rebuild stop'
            start_method (str): method to start rebuild with. Must be in
                - 'dmg pool rebuild start'
                - 'dmg system rebuild start'
        """
        dmg = self.get_dmg_command()

        self.log_step('Verify pool state before rebuild')
        for pool in pools:
            self.__verify_pool_query(
                pool, rebuild_status=0, rebuild_state=['idle', 'done'], disabled_ranks=[])

        ranks_to_exclude = self.random.sample(
            list(self.server_managers[0].ranks.keys()), k=num_ranks_to_exclude)
        self.log_step(f'{exclude_method} - Exclude random rank {ranks_to_exclude}')
        if exclude_method == 'dmg pool exclude':
            for pool in pools:
                pool.exclude(ranks_to_exclude)
        elif exclude_method == 'dmg system exclude':
            dmg.system_exclude(ranks_to_exclude)
        else:
            self.fail(f'Unsupported exclude_method: {exclude_method}')

        self.log_step(f'{exclude_method} - Manually stop rebuild with {stop_method}')
        if stop_method == 'dmg pool rebuild stop':
            for pool in pools:
                pool.rebuild_stop_retry()
        elif stop_method == 'dmg system rebuild stop':
            dmg.system_rebuild_stop_retry()
        else:
            self.fail(f'Unsupported stop_method: {stop_method}')

        self.log_step(f'{exclude_method} - Wait for rebuild to stop')
        for pool in pools:
            pool.wait_for_rebuild_to_stop(interval=3)

        self.log_step(f'{exclude_method} - Verify pool state after rebuild stopped')
        for pool in pools:
            self.__verify_pool_query(
                pool, rebuild_status=-2027, rebuild_state=['idle'],
                disabled_ranks=ranks_to_exclude)

        self.log_step(f'{exclude_method} - Verify IOR after rebuild stopped')
        for ior in iors:
            ior.run(display_space=False)

        self.log_step(f'{exclude_method} - Manually start rebuild with {start_method}')
        if start_method == 'dmg pool rebuild start':
            for pool in pools:
                pool.rebuild_start()
        elif start_method == 'dmg system rebuild start':
            dmg.system_rebuild_start()
        else:
            self.fail(f'Unsupported start_method: {start_method}')

        self.log_step(f'{exclude_method} - Wait for rebuild to start')
        for pool in pools:
            pool.wait_for_rebuild_to_start(interval=1)

        self.log_step(f'{exclude_method} - Wait for rebuild to end')
        for pool in pools:
            pool.wait_for_rebuild_to_end(interval=3)

        self.log_step(f'{exclude_method} - Verify pool state after rebuild completed')
        for pool in pools:
            self.__verify_pool_query(
                pool, rebuild_status=0, rebuild_state=['idle', 'done'],
                disabled_ranks=ranks_to_exclude)

        self.log_step(f'{exclude_method} - Verify IOR after rebuild completed')
        for ior in iors:
            ior.run(display_space=False)

        if exclude_method == 'dmg system exclude':
            self.log_step(f'{exclude_method} - Clear exclusion of ranks')
            dmg.system_clear_exclude(ranks_to_exclude)
            self.log_step(f'{exclude_method} - Start previously admin-excluded ranks')
            dmg.system_start(ranks_to_exclude)

        self.log_step(f'{reint_method} - Reintegrate excluded ranks')
        if reint_method == 'dmg pool reintegrate':
            for pool in pools:
                pool.reintegrate(ranks_to_exclude)
        elif reint_method == 'dmg system reintegrate':
            dmg.system_reintegrate(ranks_to_exclude)
        else:
            self.fail(f'Unsupported reint_method: {reint_method}')

        self.log_step(f'{reint_method} - Manually stop rebuild with {stop_method}')
        if stop_method == 'dmg pool rebuild stop':
            for pool in pools:
                pool.rebuild_stop_retry()
        elif stop_method == 'dmg system rebuild stop':
            dmg.system_rebuild_stop_retry()
        else:
            self.fail(f'Unsupported stop_method: {stop_method}')

        self.log_step(f'{reint_method} - Wait for rebuild to stop')
        for pool in pools:
            pool.wait_for_rebuild_to_stop(interval=3)

        self.log_step(f'{reint_method} - Verify pool state after rebuild stopped')
        for pool in pools:
            self.__verify_pool_query(
                pool, rebuild_status=-2027, rebuild_state=['idle'],
                disabled_ranks=[])

        self.log_step(f'{reint_method} - Verify IOR after rebuild stopped')
        for ior in iors:
            ior.run(display_space=False)

        self.log_step(f'{reint_method} - Manually start rebuild with {start_method}')
        if start_method == 'dmg pool rebuild start':
            for pool in pools:
                pool.rebuild_start()
        elif start_method == 'dmg system rebuild start':
            dmg.system_rebuild_start()
        else:
            self.fail(f'Unsupported start_method: {start_method}')

        self.log_step(f'{reint_method} - Wait for rebuild to start')
        for pool in pools:
            pool.wait_for_rebuild_to_start(interval=1)

        self.log_step(f'{reint_method} - Wait for rebuild to end')
        for pool in pools:
            pool.wait_for_rebuild_to_end(interval=3)

        self.log_step(f'{reint_method} - Verify pool state after rebuild completed')
        for pool in pools:
            self.__verify_pool_query(
                pool, rebuild_status=0, rebuild_state=['idle', 'done'], disabled_ranks=[])

        self.log_step(f'{reint_method} - Verify IOR after rebuild completed')
        for ior in iors:
            ior.run(display_space=False)

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
