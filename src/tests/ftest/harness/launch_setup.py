"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re

from apricot import TestWithoutServers

from run_utils import run_remote
from user_utils import getent


class HarnessLaunchSetupTest(TestWithoutServers):
    """Harness pre-launch test cases.

    :avocado: recursive
    """

    def test_harness_launch_setup_users(self):
        """Verify all expected users are setup correctly by launch.py -u.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness
        :avocado: tags=test_harness_launch_setup_users
        """
        hostlist_clients = self.get_hosts_from_yaml(
            "test_clients", "server_partition", "server_reservation", "/run/hosts/*")
        expected_users = self.params.get('client_users', '/run/*')
        for user_group in expected_users:
            user, group = user_group.split(':')
            self.log.info('Checking if group %s exists', group)
            group_result = getent(self.log, hostlist_clients, 'group', group)
            if not group_result.passed:
                self.fail('Group {} does not exist'.format(group))
            gid = group_result.output[0].stdout[0].split(':')[2]
            self.log.info('Querying user %s', user)
            user_result = run_remote(self.log, hostlist_clients, f'id {user}')
            if not user_result.passed:
                self.fail('Error querying user {}'.format(user))
            if not re.findall(f'groups={gid}\(', user_result.output[0].stdout[0]):
                self.fail('User {} groups not as expected'.format(user))
