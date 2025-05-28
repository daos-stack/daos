"""
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
import time
from functools import partial

from apricot import TestWithServers
from data_utils import assert_val_in_list
from exception_utils import CommandFailure
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
        # Can run only a subset of scenarios for debugging
        scenarios_to_verify = set(self.params.get('scenarios_to_verify', '/run/test/*', ['all']))

        self.log_step('Setup pool')
        pool = self.get_pool(connect=False)
        dmg = self.get_dmg_command()

        # TODO calculate this. hard-coded for now
        # The detection delay shall be a couple of SWIM periods (1s) + SWIM suspicion timeout (20s)
        # + CRT_EVENT_DELAY (1s) + some margin of error (?)
        detection_delay = 30

        # Get 2 distinct sets of ranks to stop
        all_ranks = list(self.server_managers[0].ranks.keys())
        ranks_x = self.random.sample(all_ranks, k=1)
        ranks_y = self.random.sample(list(set(all_ranks) - set(ranks_x)), k=1)

        #
        # Scenario 1: System Creation and default self_heal
        #
        if not set([1, 'all']) & scenarios_to_verify:
            self.log.warning('Skipping Scenario 1')
        else:
            self.log_step('Scenario 1 - Verify default system self_heal policy')
            response = dmg.system_get_prop(properties='self_heal')['response']
            actual_value = response[0]['value']
            expected_value = 'exclude;pool_exclude;pool_rebuild'
            if actual_value != expected_value:
                self.fail(
                    f'Expected system self_heal policy to be {expected_value}, '
                    f'but got {actual_value}')

            self.log_step('Scenario 1 - Verify default pool self_heal policy')
            response = pool.get_prop(name='self_heal')['response']
            actual_value = response[0]['value']
            expected_value = 'exclude;rebuild'
            if actual_value != expected_value:
                self.fail(
                    f'Expected pool self_heal policy to be {expected_value}, '
                    f'but got {actual_value}')

        #
        # Scenario 2: Disabling and Enabling Self-Heal
        #
        if not set([2, 'all']) & scenarios_to_verify:
            self.log.warning('Skipping Scenario 2')
        else:
            self.log_step('Scenario 2 - Disable system self_heal')
            dmg.system_set_prop('self_heal:none')

            self.log_step('Scenario 2 - Stop a rank and verify it is not excluded')
            dmg.system_stop(ranks=ranks_x)
            self.server_managers[0].update_expected_states(ranks_x, 'stopped')
            self.log.info('Waiting for detection delay of %s seconds', detection_delay)
            time.sleep(detection_delay)
            self.__verify_rank_state(ranks_x, 'stopped')
            # TODO we cannot query the pool, which means we can't check rebuild status

            self.log_step(
                'Scenario 2 - Enable system self_heal and invoke dmg system self-heal eval')
            dmg.system_set_prop('self_heal:exclude;pool_exclude;pool_rebuild')
            try:
                dmg.system_self_heal_eval()
            except CommandFailure:
                # TODO workaround for now
                self.log.error('dmg system self-heal eval failed - retrying')
                time.sleep(1)
                dmg.system_self_heal_eval()
            self.server_managers[0].update_expected_states(ranks_x, ['stopped', 'excluded'])

            self.log_step('Scenario 2 - Verify ranks are excluded and rebuilt in the pool')
            self.__verify_rank_state(ranks_x, 'excluded')
            pool.wait_for_rebuild_to_start(interval=1)
            pool.wait_for_rebuild_to_end(interval=3)

            self.log_step(
                'Scenario 2 - Stop another rank and verify it is excluded and rebuilt in the pool')
            dmg.system_stop(ranks=ranks_y)
            self.server_managers[0].update_expected_states(ranks_y, ['stopped', 'excluded'])
            pool.wait_for_rebuild_to_start(interval=1)
            pool.wait_for_rebuild_to_end(interval=3)
            self.__verify_rank_state(ranks_y, 'excluded')

            self.log_step(
                'Scenario 2 - Reintegrate stopped ranks to bring system back to original state')
            stopped_ranks_str = list_to_str(ranks_x + ranks_y)
            dmg.system_start(stopped_ranks_str)
            dmg.system_reintegrate(stopped_ranks_str)
            self.server_managers[0].update_expected_states(ranks_x + ranks_y, ['joined'])
            pool.wait_for_rebuild_to_start(interval=1)
            pool.wait_for_rebuild_to_end(interval=3)
            self.__verify_rank_state(all_ranks, 'joined')

        #
        # Scenario 3: Online System Maintenance
        #
        if not set([3, 'all']) & scenarios_to_verify:
            self.log.warning('Skipping Scenario 3')
        else:
            self.log_step('Scenario 3 - Set system.self_heal.pool_rebuild = disabled')
            dmg.system_set_prop('self_heal:exclude;pool_exclude')
            dmg.system_get_prop(properties='self_heal')

            self.log_step('Scenario 3 - Stop a rank and verify it is excluded without rebuild')
            dmg.system_stop(ranks=ranks_x)
            self.server_managers[0].update_expected_states(ranks_x, ['stopped', 'excluded'])
            self.log.info('Waiting for detection delay of %s seconds', detection_delay)
            time.sleep(detection_delay)
            self.__verify_rank_state(ranks_x, 'excluded')
            pool.verify_query({
                'disabled_ranks': ranks_x,
                'rebuild': {
                    'state': partial(assert_val_in_list, allowed_list=['done', 'idle'])}})
            # Targets should be down but not down_out
            pool.verify_query_targets_state(ranks_x, 'down')

            self.log_step('Scenario 3 - Restart the rank and make sure it rejoins')
            dmg.system_start(ranks=ranks_x)
            self.server_managers[0].update_expected_states(ranks_x, ['joined'])
            self.__verify_rank_state(all_ranks, 'joined', tries=5, delay=3)

            self.log_step('Scenario 3 - Reintegrate the rank and wait for rebuild')
            dmg.system_reintegrate(list_to_str(ranks_x))
            self.server_managers[0].update_expected_states(ranks_x, ['joined'])
            pool.wait_for_rebuild_to_start(interval=1)
            pool.wait_for_rebuild_to_end(interval=3)

            # The pool version changes after exclusion,
            # but should not changed after resetting self_heal
            self.log.info('Save current pool version')
            pool_version = pool.query()['response']['version']

            self.log_step('Scenario 3 - Reset system self_heal to default')
            dmg.system_set_prop('self_heal:exclude;pool_exclude;pool_rebuild')

            self.log_step('Scenario 3 - Verify dmg system self-heal eval does not trigger rebuild')
            dmg.system_self_heal_eval()
            self.log.info('Waiting for detection delay of %s seconds', detection_delay)
            time.sleep(detection_delay)
            pool.verify_query({
                'disabled_ranks': [],
                'rebuild': {
                    'state': partial(assert_val_in_list, allowed_list=['done', 'idle'])},
                'version': pool_version})

        #
        # Scenario 4: Offline System Maintenance
        #
        if not set([4, 'all']) & scenarios_to_verify:
            self.log.warning('Skipping Scenario 4')
        else:
            self.log_step('Scenario 4 - Disable system self_heal')
            dmg.system_set_prop('self_heal:none')

            # We expect the pool version to stay the same through this scenario since
            # there are no exclusions or rebuilds
            self.log.info('Save current pool version')
            pool_version = pool.query()['response']['version']

            self.log_step(
                'Scenario 4 - Stop more ranks than the pool RF and verify there are no exclusions')
            pool_rf = int(re.findall(r'rd_fac:([0-9]+)', pool.properties.value)[0])
            self.assertGreater(
                len(all_ranks), pool_rf, 'Not enough ranks to stop more than pool RF')
            ranks_over_rf = self.random.sample(all_ranks, k=pool_rf + 1)
            dmg.system_stop(ranks=list_to_str(ranks_over_rf))
            self.server_managers[0].update_expected_states(ranks_over_rf, ['stopped'])
            self.log.info('Waiting for detection delay of %s seconds', detection_delay)
            time.sleep(detection_delay)
            self.__verify_rank_state(ranks_over_rf, 'stopped')

            self.log_step('Scenario 4 - Restart the stopped ranks and make sure they rejoin')
            dmg.system_start(ranks=list_to_str(ranks_over_rf))
            self.server_managers[0].update_expected_states(ranks_over_rf, ['joined'])
            self.__verify_rank_state(all_ranks, 'joined', tries=5, delay=3)

            self.log_step('Scenario 4 - Reset system self_heal to default')
            dmg.system_set_prop('self_heal:exclude;pool_exclude;pool_rebuild')

            self.log_step('Scenario 4 - Verify dmg system self-heal eval does not trigger rebuild')
            dmg.system_self_heal_eval()
            self.log.info('Waiting for detection delay of %s seconds', detection_delay)
            time.sleep(detection_delay)
            pool.verify_query({
                'disabled_ranks': [],
                'rebuild': {
                    'state': partial(assert_val_in_list, allowed_list=['done', 'idle'])},
                'version': pool_version})

        #
        # Scenario 5: Normal System Restart
        #
        if not set([5, 'all']) & scenarios_to_verify:
            self.log.warning('Skipping Scenario 5')
        else:
            self.log_step('Scenario 5 - Disable system self_heal')
            dmg.system_set_prop('self_heal:none')

            # We expect the pool version to stay the same through this scenario since
            # there are no exclusions or rebuilds
            self.log.info('Save current pool version')
            pool_version = pool.query()['response']['version']

            self.log_step('Scenario 5 - Stop the system and verify no ranks are excluded')
            dmg.system_stop()
            self.server_managers[0].update_expected_states(all_ranks, ['stopped'])
            self.log.info('Waiting for detection delay of %s seconds', detection_delay)
            time.sleep(detection_delay)
            self.__verify_rank_state(all_ranks, 'stopped')

            self.log_step('Scenario 5 - Restart the system and make sure all ranks rejoin')
            dmg.system_start()
            self.server_managers[0].update_expected_states(all_ranks, ['joined'])
            self.__verify_rank_state(all_ranks, 'joined', tries=5, delay=3)

            self.log_step('Scenario 5 - Reset system self_heal to default')
            dmg.system_set_prop('self_heal:exclude;pool_exclude;pool_rebuild')

            self.log_step('Scenario 5 - Verify dmg system self-heal eval does not trigger rebuild')
            dmg.system_self_heal_eval()
            self.log.info('Waiting for detection delay of %s seconds', detection_delay)
            time.sleep(detection_delay)
            pool.verify_query({
                'disabled_ranks': [],
                'rebuild': {
                    'state': partial(assert_val_in_list, allowed_list=['done', 'idle'])},
                'version': pool_version})

        #
        # Scenario 6: Unexpected System Restart
        #
        if not set([6, 'all']) & scenarios_to_verify:
            self.log.warning('Skipping Scenario 6')
        else:
            self.log_step('Scenario 6 - Simulate restart with dmg system stop')

            # We expect the pool version to stay the same through this scenario since
            # there are no exclusions or rebuilds
            self.log.info('Save current pool version')
            pool_version = pool.query()['response']['version']
            dmg.system_stop()
            self.server_managers[0].update_expected_states(all_ranks, ['stopped'])

            self.log_step('Scenario 6 - Start all but 1 rank and immediately disable self-heal')
            all_ranks_minus_1 = self.random.sample(all_ranks, k=len(all_ranks) - 1)
            dmg.system_start(ranks=list_to_str(all_ranks_minus_1))
            self.server_managers[0].update_expected_states(all_ranks_minus_1, ['joined'])
            dmg.system_set_prop('self_heal:none')

            self.log_step('Scenario 6 - Verify all but 1 rank rejoins')
            self.__verify_rank_state(all_ranks_minus_1, 'joined', tries=5, delay=3)

            self.log_step('Scenario 6 - Restart the last rank and make sure it rejoins')
            dmg.system_start()
            self.server_managers[0].update_expected_states(all_ranks, ['joined'])
            self.__verify_rank_state(all_ranks, 'joined', tries=5, delay=3)

            self.log_step('Scenario 6 - Reset system self_heal to default')
            dmg.system_set_prop('self_heal:exclude;pool_exclude;pool_rebuild')

            self.log_step('Scenario 6 - Verify dmg system self-heal eval does not trigger rebuild')
            dmg.system_self_heal_eval()
            self.log.info('Waiting for detection delay of %s seconds', detection_delay)
            time.sleep(detection_delay)
            pool.verify_query({
                'disabled_ranks': [],
                'rebuild': {
                    'state': partial(assert_val_in_list, allowed_list=['done', 'idle'])},
                'version': pool_version})

        #
        # Scenario 7: Problematic Pools
        #
        if not set([7, 'all']) & scenarios_to_verify:
            self.log.warning('Skipping Scenario 7')
        else:
            self.log_step('Scenario 7 - Create a second pool')
            pool2 = self.get_pool(connect=False)

            self.log_step('Scenario 7 - Disable self_heal rebuild on just the second pool')
            pool2.set_prop('self_heal:exclude')
            pool2.query()

            self.log_step('Scenario 7 - Stop a rank and wait for the detection delay')
            dmg.system_stop(ranks=ranks_x)
            self.server_managers[0].update_expected_states(ranks_x, ['stopped', 'excluded'])
            time.sleep(detection_delay)

            self.log_step(
                'Scenario 7 - Verify the rank is excluded and rebuilds in first pool only')
            self.__verify_rank_state(ranks_x, 'excluded')
            pool.wait_for_rebuild_to_start(interval=1)
            pool.wait_for_rebuild_to_end(interval=3)
            pool.verify_query({
                'disabled_ranks': ranks_x,
                'rebuild': {
                    'state': 'done'}})
            self.log_step(
                'Scenario 7 - Verify the rank is excluded and does not rebuild in second pool')
            pool2.verify_query({
                'disabled_ranks': ranks_x,
                'rebuild': {
                    'state': partial(assert_val_in_list, allowed_list=['done', 'idle'])}})
            # Targets should be down but not down_out
            pool2.verify_query_targets_state(ranks_x, 'down')

            self.log_step(
                'Scenario 7 - Reintegrate stopped ranks to bring system back to original state')
            stopped_ranks_str = list_to_str(ranks_x)
            dmg.system_start(stopped_ranks_str)
            dmg.system_reintegrate(stopped_ranks_str)
            self.server_managers[0].update_expected_states(ranks_x, ['joined'])
            pool.wait_for_rebuild_to_start(interval=1)
            pool.wait_for_rebuild_to_end(interval=3)
            self.__verify_rank_state(all_ranks, 'joined')

            self.log_step('Scenario 7 - Destroy second pool')
            pool2.destroy()
            del pool2

        self.log_step('Destroy pool')
        pool.destroy()

        self.log_step('Test passed')

    def __verify_rank_state(self, ranks, state, tries=1, delay=3):
        """Verify the state of the given ranks.

        Args:
            ranks (list): The list of ranks to verify.
            state (str): The expected state of the ranks.
            tries (int, optional): Number of attempts to verify the state. Defaults to 1.
            delay (int, optional): Delay between attempts in seconds. Defaults to 3.
        """
        for current_try in range(tries):
            current_state = self.server_managers[0].get_current_state()

            # All ranks are in expected state
            if set(current_state[rank]['state'] for rank in ranks) == {state}:
                return

            # Retry
            if current_try < tries - 1:
                self.log.info(
                    'Not all ranks are in expected state %s. Retrying in %s seconds...',
                    state, delay)
                time.sleep(delay)
                continue

            # Final attempt failed
            for rank in ranks:
                if current_state[rank]['state'] != state:
                    self.fail(
                        f'Expected rank {rank} to be in state {state}, '
                        f'but current state is {current_state[rank]["state"]}')
