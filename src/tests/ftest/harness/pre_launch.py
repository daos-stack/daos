"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re

from apricot import TestWithoutServers

from run_utils import run_remote
from util.user_utils import get_getent_command


class HarnessPreLaunchTest(TestWithoutServers):
    """Harness pre-launch test cases.

    :avocado: recursive
    """

    def test_harness_pre_launch_users(self):
        """Verify all expected users are setup correctly with launch.py --user_setup.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness
        :avocado: tags=test_harness_pre_launch_users
        """
        hostlist_clients = self.get_hosts_from_yaml(
            "test_clients", "server_partition", "server_reservation", "/run/hosts/*")
        expected_users = self.params.get('users', '/run/test_harness_pre_launch_users/*')
        for user_group in expected_users:
            user, group = user_group.split(':')
            self.log.info('Checking if group %s exists', group)
            group_result = run_remote(
                self.log, hostlist_clients, get_getent_command('group', group))
            if not group_result.passed:
                self.fail(f'Group {group} does not exist')
            gid = group_result.output[0].stdout[0].split(':')[2]
            self.log.info('Checking if user %s exists', user)
            user_result = run_remote(self.log, hostlist_clients, f'id {user}')
            if not user_result.passed:
                self.fail(f'User {user} does not exist')
            self.log.info('Checking if user %s is in group %s', user, gid)
            if not re.findall(f'gid={gid}', user_result.output[0].stdout[0]):
                self.fail(f'User {user} not in group {group}')
