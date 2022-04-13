#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from apricot import TestWithServers
from general_utils import pcmd, stop_processes


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

    def test_daos_server_dump_basic(self):
        """JIRA ID: DAOS-1452.

        Test Description: Test engine ULT stacks dump.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,server_start,basic
        :avocado: tags=daos_server_dump_test,test_daos_server_dump_basic
        """

        ret_codes = stop_processes(self.hostlist_servers, r"daos_engine",
                           added_filter=r"'\<(grep|defunct)\>'",
                           dump_ult_stacks=True)
        if 1 in ret_codes:
            print(
                "Stopped daos_engine processes on {}".format(
                    str(ret_codes[1])))
        if 0 in ret_codes:
            print(
                "No daos_engine processes found on {}".format(
                    str(ret_codes[0])))

        # XXX may need to check for one file per engine...
        ret_codes = pcmd(self.hostlist_servers, r"ls /tmp/daos_dump*.txt")
        # Report any failures
        if len(ret_codes) > 1 or 0 not in ret_codes:
            failed = [
                "{}: rc={}".format(val, key)
                for key, val in ret_codes.items() if key != 0
            ]
            self.fail(
                "no ULT stacks dump found on following hosts: {}".format(
                ", ".join(failed)))

        self.log.info("Test passed!")

        # set stopped servers state to make teardown happy
        self.server_managers[0].update_expected_states(
            None, ["stopped", "excluded", "errored"])
