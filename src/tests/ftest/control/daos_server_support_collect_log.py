"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from support_test_base import SupportTestBase


class DaosSupportCollectLogTest(SupportTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:Verify the daos_server support collect-log command.

    :avocado: recursive
    """

    def test_daos_support_collect_log_with_archive(self):
        """JIRA ID: DAOS-10625

        Test Description:
            Test daos_server support collect-log command completes successfully.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,basic,support,daos_server
        :avocado: tags=test_daos_server_support_collect_log
        """
        self.log_hosts = self.hostlist_servers
        # Create the custom log data which will be collected via support collect-log,
        # Later verify the dame data file is archived as part of collection.
        self.create_custom_log("Server_Support_Logs")

        # Run daos_server support collect-log with --extra-logs-dir
        # Copy the log to non default folder with command option --target-folder
        # Enable archive mode to collect the logs
        result = self.server_managers[0].support_collect_log(
            extra_logs_dir=self.custom_log_dir,
            target_folder=self.target_folder,
            archive=True)

        if not result.passed:
            self.fail("Failed to run daos_server support collect-log command")

        # Extract the collected tar.gz file
        result = self.extract_logs(self.target_folder + ".tar.gz")
        if result is not None:
            self.fail(result)

        # Verify server logs file in extracted dir
        result = self.validate_server_log_files()
        if result is not None:
            self.fail(result)

        # Verify the custom log file collected on each servers.
        self.verify_custom_log_data()

        # Clean up the log file created during test execution
        self.cleanup_support_log(self.target_folder)
