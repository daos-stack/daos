"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re

from apricot import TestWithoutServers

from run_utils import run_remote


class HarnessPreLaunchTest(TestWithoutServers):
    """Harness pre-launch test cases.

    :avocado: recursive
    """

    def test_harness_pre_launch_users(self):
        """Verify all expected users are setup correctly.

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
            group_result = run_remote(self.log, hostlist_clients, 'getent group {}'.format(group))
            if not group_result.passed:
                self.fail('Group {} does not exist'.format(group))
            gid = group_result.output[0].stdout[0].split(':')[2]
            user_result = run_remote(self.log, hostlist_clients, 'id {}'.format(user))
            if not user_result.passed:
                self.fail('User {} does not exist'.format(user))
            if not re.findall(r'gid={}'.format(gid), user_result.output[0].stdout[0]):
                self.fail('User {} not in group {}'.format(user, group))
