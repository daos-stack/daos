#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from apricot import TestWithServers
from general_utils import pcmd, dump_engines_stacks

import time

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

    def tearDown(self):
        """Tear down after each test case."""
        super().tearDown()

        # force test status !!
        self.__status = 'PASS'

        # XXX may need to check for one file per engine...
        ret_codes = pcmd(self.hostlist_servers, r"ls /tmp/daos_dump*.txt")
        # Report any failures
        if len(ret_codes) > 1 or 0 not in ret_codes:
            failed = [
                "{}: rc={}".format(val, key)
                for key, val in ret_codes.items() if key != 0
            ]
            print(
                "no ULT stacks dump found on following hosts: {}".format(
                ", ".join(failed)))
            self.__status = 'FAIL'

    def test_daos_server_dump_basic(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,server_start,basic
        :avocado: tags=daos_server_dump_tests,test_daos_server_dump_basic
        """

        ret_codes = dump_engines_stacks(self.hostlist_servers,
                           added_filter=r"'\<(grep|defunct)\>'")
        # at this time there is no way to know when Argobots ULTs stacks
        # has completed, see DAOS-1452/DAOS-9942.
        if 1 in ret_codes:
            print(
                "Dumped daos_engine stacks on {}".format(
                    str(ret_codes[1])))
        if 0 in ret_codes:
            self.fail(
                "No daos_engine processes found on {}".format(
                    str(ret_codes[0])))

        self.log.info("Test passed!")

    def test_daos_server_dump_on_error(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,server_start,basic
        :avocado: tags=daos_server_dump_tests,test_daos_server_dump_on_error
        """

        self.error()

    def test_daos_server_dump_on_fail(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,server_start,basic
        :avocado: tags=daos_server_dump_tests,test_daos_server_dump_on_fail
        """

        self.fail()

    def test_daos_server_dump_on_timeout(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,server_start,basic
        :avocado: tags=daos_server_dump_tests,test_daos_server_dump_on_timeout
        """

        self.timeout = 1
        time.sleep(2)
        self.report_timeout()

    def test_daos_server_dump_on_unexpected_engine_status(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,server_start,basic
        :avocado: tags=daos_server_dump_tests,test_daos_server_dump_on_unexpected_engine_status
        """

        # set stopped servers state to make teardown unhappy
        self.server_managers[0].update_expected_states(
            None, ["stopped", "excluded", "errored"])
