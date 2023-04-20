"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from general_utils import dump_engines_stacks
from run_utils import run_remote


class DaosServerDumpTest(TestWithServers):
    """Daos server dump tests.

    Test Class Description:
        Simple test to verify that ULT stacks dumps work

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DaosServerDumpTest object."""
        super().__init__(*args, **kwargs)
        self.start_servers_once = False
        self.setup_start_agents = False

        # force test status !!
        # use mangling trick described at
        # https://stackoverflow.com/questions/3385317/private-variables-and-methods-in-python
        # conditionally set to FAIL in tearDown
        self._Test__status = 'PASS'  # pylint:disable=invalid-name

    def tearDown(self):
        """Tear down after each test case."""
        super().tearDown()

        # DAOS-1452 may need to check for one file per engine...
        result = run_remote(self.log, self.hostlist_servers, r"ls /tmp/daos_dump*.txt")
        if not result.passed:
            self.log.info("no ULT stacks dump found on following hosts: %s", result.failed_hosts)
            self._Test__status = 'FAIL'

    def test_daos_server_dump_basic(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump (basic).

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=server
        :avocado: tags=DaosServerDumpTest,test_daos_server_dump_basic
        """

        ret_codes = dump_engines_stacks(self.hostlist_servers,
                                        added_filter=r"'\<(grep|defunct)\>'")
        # at this time there is no way to know when Argobots ULTs stacks
        # has completed, see DAOS-1452/DAOS-9942.
        if 1 in ret_codes:
            self.log.info("Dumped daos_engine stacks on %s", str(ret_codes[1]))
        if 0 in ret_codes:
            self.fail("No daos_engine processes found on {}".format(str(ret_codes[0])))

        self.log.info("Test passed!")

    def test_daos_server_dump_on_error(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump (error case).

        :avocado: tags=manual
        :avocado: tags=vm
        :avocado: tags=server
        :avocado: tags=DaosServerDumpTest,test_daos_server_dump_on_error
        """
        self.log.info("Forcing test error!")
        self.error("Forcing test error!")

    def test_daos_server_dump_on_fail(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump (failure case).

        :avocado: tags=manual
        :avocado: tags=vm
        :avocado: tags=server
        :avocado: tags=DaosServerDumpTest,test_daos_server_dump_on_fail
        """
        self.log.info("Forcing test failure!")
        self.fail("Forcing test failure!")

    def test_daos_server_dump_on_timeout(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump (timeout case).

        :avocado: tags=manual
        :avocado: tags=vm
        :avocado: tags=server
        :avocado: tags=DaosServerDumpTest,test_daos_server_dump_on_timeout
        """
        self.log.info("Sleeping to trigger test timeout!")
        time.sleep(30)

    def test_daos_server_dump_on_unexpected_engine_status(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump (unexpected engine status case).

        :avocado: tags=manual
        :avocado: tags=vm
        :avocado: tags=server
        :avocado: tags=DaosServerDumpTest,test_daos_server_dump_on_unexpected_engine_status
        """
        self.log.info("Forcing servers expected state to make teardown unhappy!")
        # set stopped servers expected state to make teardown unhappy
        self.server_managers[0].update_expected_states(
            None, ["stopped", "excluded", "errored"])
