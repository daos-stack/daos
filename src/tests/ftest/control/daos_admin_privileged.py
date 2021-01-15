#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

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

    def __init__(self, *args, **kwargs):
        """Initialize a DaosAdminPrivTest object."""
        super(DaosAdminPrivTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False
        self.setup_start_servers = False

    def test_daos_admin_format(self):
        """JIRA ID: DAOS-2895.

        Test Description:
            Test daos_admin functionality to perform format privileged
            operations while daos_server is run as normal user.

        :avocado: tags=all,pr,daily_regression,hw,small,daos_admin,basic
        """
        # Verify that daos_admin has the correct permissions
        self.log.info("Checking daos_admin binary permissions")
        file_stats = os.stat("/usr/bin/daos_admin")

        # regular file, mode 4750
        desired = (stat.S_IFREG|stat.S_ISUID|
                   stat.S_IRWXU|stat.S_IRGRP|stat.S_IXGRP)
        actual = file_stats.st_mode & ~stat.S_IRWXO # mask out Other
                                                    # bits for non-RPM
        if (actual ^ desired) > 0:
            self.fail("Incorrect daos_admin permissions: {}".format(
                oct(actual)))

        # Setup server as non-root
        self.log.info("Preparing to run daos_server as non-root user")
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
            self.fail("Failed preparing SCM as user {}: {}".format(user, error))

        # Uncomment the below line after DAOS-4287 is resolved
        # # Prep server for format, run command under non-root user
        # self.log.info("Performing NVMe storage prepare")
        # try:
        #     self.server_managers[0].prepare_storage(user, False, True)
        # except ServerFailed as err:
        #     self.fail(
        #         "Failed preparing NVMe as user {}: {}".format(user, err))

        # Start server
        self.log.info("Starting server as non-root")
        try:
            self.server_managers[0].detect_format_ready()
        except ServerFailed as error:
            self.fail(
                "Failed starting server before format as non-root user: "
                "{}".format(error))

        # Run format command under non-root user
        self.log.info("Performing SCM format")
        result = self.server_managers[0].dmg.storage_format()
        if result is None:
            self.fail("Failed to format storage")

        # Verify format success when all the doas_io_servers start
        try:
            self.server_managers[0].detect_io_server_start()
        except ServerFailed as error:
            self.fail(
                "Failed starting server after format as non-root user: "
                "{}".format(error))
