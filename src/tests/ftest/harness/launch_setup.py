"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithoutServers
from run_utils import command_as_user, run_remote
from user_utils import get_group_id, get_user_groups


class HarnessLaunchSetupTest(TestWithoutServers):
    """Harness launch.py setup test cases.

    :avocado: recursive
    """

    def test_harness_launch_setup_users(self):
        """Verify all expected users are setup correctly by launch.py -u.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness
        :avocado: tags=HarnessLaunchSetupTest,test_harness_launch_setup_users
        """
        hostlist_clients = self.get_hosts_from_yaml(
            "test_clients", "server_partition", "server_reservation", "/run/hosts/*")
        expected_users = self.params.get('client_users', '/run/*')
        for user_group in expected_users:
            user, group = user_group.split(':')
            self.log.info('Checking if group %s exists', group)
            gids = get_group_id(self.log, hostlist_clients, group).keys()
            self.log.info('  found gids %s', gids)
            gids = list(gids)
            if len(gids) != 1 or gids[0] is None:
                self.fail('Group not setup correctly: {}'.format(group))
            self.log.info('Querying user %s', user)
            groups = get_user_groups(self.log, hostlist_clients, user)
            self.log.info('  found groups %s', groups)
            groups = list(groups)
            if len(groups) != 1 or groups[0] != gids[0]:
                self.fail('User {} groups not as expected'.format(user))
            self.log.info('Checking if user %s can run commands', user)
            user_result_self = run_remote(self.log, hostlist_clients, command_as_user('id', user))
            if not user_result_self.passed:
                self.fail('Error running command as user {}'.format(user))
