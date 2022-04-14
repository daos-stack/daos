#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import os
import stat
import getpass

from apricot import TestWithServers
from server_utils import ServerFailed


class DaosAdminPrivTest(TestWithServers):
    """Test class for daos_admin privilege tests.

    Test Class Description:
        Test to verify that daos_server when run as normal user, can perform
        privileged functions.

    :avocado: recursive
    """

    def test_daos_admin_format(self):
        """JIRA ID: DAOS-2895.

        Test Description:
            Test daos_admin functionality to perform format privileged
            operations while daos_server is run as normal user.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=daos_admin,basic
        """
        # Verify that daos_admin has the correct permissions
        self.log.info("Checking daos_admin binary permissions")
        file_stats = os.stat("/usr/bin/daos_admin")

        # regular file, mode 4750
        desired = (stat.S_IFREG | stat.S_ISUID | stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP)
        actual = file_stats.st_mode & ~stat.S_IRWXO  # mask out Other bits for non-RPM
        if (actual ^ desired) > 0:
            self.fail("Incorrect daos_admin permissions: {}".format(oct(actual)))

        # Setup server as non-root
        self.log.info("(0)Preparing to run daos_server as non-root user")
        self.add_server_manager()
        self.configure_manager(
            "server", self.server_managers[0], self.hostlist_servers,
            self.hostfile_servers_slots)
        self.server_managers[0].prepare(False)

        # Get user
        user = getpass.getuser()

        # Prep server for format, run command under non-root user
        self.log.info("Performing SCM storage prepare")
        try:
            self.server_managers[0].prepare_storage(user, True, False)
        except ServerFailed as error:
            self.fail("(1)Failed preparing SCM as user {}: {}".format(user, error))

        self.log.info("(2)Performing NVMe storage prepare")
        try:
            self.server_managers[0].prepare_storage(user, False, True)
        except ServerFailed as err:
            self.fail("##(2)Failed preparing NVMe as user {}: {}".format(user, err))

        # Start server
        self.log.info("(3)Starting server as non-root")
        try:
            self.server_managers[0].detect_format_ready()
        except ServerFailed as error:
            self.fail(
                "##(3)Failed starting server before format as non-root user: "
                "{}".format(error))

        # Run format command under non-root user
        self.log.info("(4)Performing SCM format")
        result = self.server_managers[0].dmg.storage_format()
        if result is None:
            self.fail("##(4)Failed to format storage")

        # Verify format success when all the daos_engine start.
        # Use dmg to detect server start.
        self.log.info(
            "(5)Verify format success when all the daos_engine start")
        try:
            self.server_managers[0].detect_start_via_dmg = True
            self.server_managers[0].detect_engine_start()
        except ServerFailed as error:
            self.fail(
                "##(5)Failed starting server after format as non-root user: "
                "{}".format(error))
