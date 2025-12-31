"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from general_utils import list_to_str


class RbldAutoRecoveryPolicy(TestWithServers):
    """Rebuild test cases featuring IOR.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    def test_rebuild_auto_recovery_policy(self):
        """Jira ID: DAOS-17420.

        Test Description: Verify Rebuild Auto Recovery Policy

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,rebuild
        :avocado: tags=RbldAutoRecoveryPolicy,test_rebuild_auto_recovery_policy
        """
        self.log_step('Setup pool')
        pool = self.get_pool(connect=False)
        dmg = self.get_dmg_command()

        # TODO calculate this. hard-coded for now
        # The detection delay shall be a couple of SWIM periods (1s) + SWIM suspicion timeout (20s)
        # + CRT_EVENT_DELAY (1s) + some margin of error (?)
        detection_delay = 30

        # SCENARIO 1: System Creation and default self_heal
        self.log_step('Verify default system and pool self_heal policy')
        self.__verify_system_prop_self_heal(dmg, 'exclude;pool_exclude;pool_rebuild')
        self.__verify_pool_prop_self_heal(pool, 'exclude;rebuild')

        # SCENARIO 2: Disabling and Enabling Self-Heal
        self.log_step('Disable system self_heal')
        dmg.system_set_prop('self_heal:none')

        # Get 2 distinct sets of ranks to stop
        all_ranks = list(self.server_managers[0].ranks.keys())
        ranks_x = self.random.sample(all_ranks, k=1)
        ranks_y = self.random.sample(list(set(all_ranks) - set(ranks_x)), k=1)

        self.log_step('Stop a rank and verify it is not excluded')
        dmg.system_stop(ranks=ranks_x)
        self.server_managers[0].update_expected_states(ranks_x, 'stopped')
        self.log.info('Waiting for detection delay of %s seconds', detection_delay)
        time.sleep(detection_delay)
        self.__verify_rank_state(ranks_x, 'stopped')
        # TODO we cannot query the pool, which could be a problem

        self.log_step('Enable system self_heal and invoke dmg system self-heal eval')
        dmg.system_set_prop('self_heal:exclude;pool_exclude;pool_rebuild')
        dmg.system_self_heal_eval()
        self.server_managers[0].update_expected_states(ranks_x, ['stopped', 'excluded'])

        self.log_step('Verify ranks are excluded and rebuilt in the pool')
        self.__verify_rank_state(ranks_x, 'excluded')
        pool.wait_for_rebuild_to_start(interval=1)
        pool.wait_for_rebuild_to_stop(interval=3)

        self.log_step('Stop another rank and verify it is excluded and rebuilt in the pool')
        dmg.system_stop(ranks=ranks_y)
        self.server_managers[0].update_expected_states(ranks_y, ['stopped', 'excluded'])
        pool.wait_for_rebuild_to_start(interval=1)
        pool.wait_for_rebuild_to_stop(interval=3)
        self.__verify_rank_state(ranks_y, 'excluded')

        self.log_step('Reintegrate stopped ranks to bring the system back to original state')
        stopped_ranks_str = list_to_str(ranks_x + ranks_y)
        dmg.system_start(stopped_ranks_str)
        dmg.system_reintegrate(stopped_ranks_str)
        self.server_managers[0].update_expected_states(ranks_x + ranks_y, ['joined'])
        pool.wait_for_rebuild_to_start(interval=1)
        pool.wait_for_rebuild_to_stop(interval=3)
        self.__verify_rank_state(all_ranks, 'joined')

        # SCENARIO 3: Online System Maintenance
        # TODO Begin the maintenance by setting system.self_heal.pool_rebuild = disabled.
        # TODO Invoke dmg system stop --ranks=X and wait for the detection delay.
        #      Rank X excluded from the system and the pool.
        #      No rebuild
        # TODO Invoke dmg system start --ranks=X and wait for
        #      dmg system query to indicate rank X joined.
        #      Rank X no longer excluded from the system
        # TODO Invoke dmg system reintegrate --ranks=X
        #      Rank X reintegrated in the pool
        #      Rank X rebuilding in the pool
        # TODO End the maintenance by setting system.self_heal.pool_rebuild = enabled and
        #      invoke dmg system self-heal eval
        #      No rebuild

    def __verify_system_prop_self_heal(self, dmg, expected_value):
        """Verify the self_heal property of the system.

        Args:
            dmg (DmgCommand): The dmg command object.
            expected_value (str): The expected self_heal property value.

        """
        response = dmg.system_get_prop(properties='self_heal')['response']
        actual_value = response[0]['value']
        if actual_value != expected_value:
            self.fail(
                f'Expected system self_heal policy to be {expected_value}, but got {actual_value}')

    def __verify_pool_prop_self_heal(self, pool, expected_value):
        """Verify the self_heal property of the pool.

        Args:
            pool (TestPool): The pool to check.
            expected_value (str): The expected self_heal property value.

        """
        response = pool.get_prop(name='self_heal')['response']
        actual_value = response[0]['value']
        if actual_value != expected_value:
            self.fail(
                f'Expected pool self_heal policy to be {expected_value}, but got {actual_value}')

    def __verify_rank_state(self, ranks, state):
        """Verify the state of the given ranks.

        Args:
            ranks (list): The list of ranks to verify.
            state (str): The expected state of the ranks.
        """
        current_state = self.server_managers[0].get_current_state()
        for rank in ranks:
            if current_state[rank]['state'] != state:
                self.fail(
                    f'Expected rank {rank} to be {state}, '
                    f'but current state is {current_state[rank]["state"]}')
