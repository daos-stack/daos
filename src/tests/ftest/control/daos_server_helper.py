"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import getpass
import os
import stat

from apricot import TestWithServers
from run_utils import run_remote
from server_utils import ServerFailed


class DaosPrivHelperTest(TestWithServers):
    """Test class for daos_server_helper privilege tests.

    Test Class Description:
        Test to verify that daos_server when run as normal user, can perform
        privileged functions.

    :avocado: recursive
    """

    def test_daos_server_helper_format(self):
        """JIRA ID: DAOS-2895.

        Test Description:
            Test daos_server_helper functionality to perform format privileged
            operations while daos_server is run as normal user.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,basic,daos_server_helper
        :avocado: tags=DaosPrivHelperTest,test_daos_server_helper_format
        """
        # Verify that daos_server_helper has the correct permissions
        # Get the result remotely with os.stat so the format is compatible with local code
        self.log_step("Verify daos_server_helper binary permissions")
        helper_path = os.path.join(self.bin, "daos_server_helper")
        cmd = f"python3 -c 'import os; print(os.stat(\"{helper_path}\").st_mode)'"
        result = run_remote(self.log, self.hostlist_servers, cmd)
        if not result.passed:
            self.fail("Failed to get daos_server_helper mode")
        if not result.homogeneous:
            self.fail("Non-homogeneous daos_server_helper mode")
        mode = int(result.joined_stdout)

        # regular file, mode 4750
        desired = stat.S_IFREG | stat.S_ISUID | stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP
        actual = mode & ~stat.S_IRWXO  # mask out Other bits for non-RPM
        if (actual ^ desired) > 0:
            self.fail(f"Incorrect daos_server_helper permissions: {oct(actual)}")

        # Setup server as non-root
        self.log_step("Prepare to run daos_server as non-root user")
        self.add_server_manager()
        self.configure_manager(
            "server", self.server_managers[0], self.hostlist_servers, self.hostfile_servers_slots)
        self.server_managers[0].prepare(False)

        # Get user
        user = getpass.getuser()

        # Prep server for format, run command under non-root user
        # Note: This will just report the presence of PMem namespaces if the NVDIMMs are already
        #       configured in AppDirect interleaved mode and namespaces have been created.
        self.log_step("Perform NVMe storage prepare as non-root")
        try:
            self.server_managers[0].prepare_storage(user, False, True)
        except ServerFailed as err:
            self.fail(f"Failed to prepare NVMe as user {user}: {err}")

        # Start server
        self.log_step("Start server as non-root")
        try:
            self.server_managers[0].detect_format_ready()
            self.register_cleanup(self.stop_servers)
        except ServerFailed as error:
            self.fail(f"Failed to start server before format as non-root user: {error}")

        # Run format command under non-root user
        self.log_step("Perform SCM format")
        result = self.server_managers[0].dmg.storage_format()
        if result is None:
            self.fail("Failed to format storage")

        # Verify format success when all the daos_engine start.
        # Use dmg to detect server start.
        self.log_step("Verify format succeeds when all the daos_engines start")
        try:
            self.server_managers[0].detect_start_via_dmg = True
            self.server_managers[0].detect_engine_start()
        except ServerFailed as error:
            self.fail(f"Failed to start server after format as non-root user: {error}")
