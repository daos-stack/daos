"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from support_test_base import SupportTestBase


class DaosSupportCollectLogTest(SupportTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:Verify the daos_server support collect-log command.

    :avocado: recursive
    """

    def test_daos_support_collect_log(self):
        """JIRA ID: DAOS-10625

        Test Description:
            Test daos_server support collect-log command completes successfully.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,basic,support,daos_server
        :avocado: tags=DaosSupportCollectLogTest,test_daos_server_support_collect_log
        """
        self.log_hosts = self.hostlist_servers
        # Create the custom log data which will be collected via support collect-log,
        # Later verify the data file is archived as part of collection.
        self.create_custom_log("Server_Support_Logs")

        # Run daos_server support collect-log with --extra-logs-dir,
        # copy log to folder with command option --target-folder
        # Enable archive mode.
        result = self.server_managers[0].support_collect_log(
            extra_logs_dir=self.custom_log_dir,
            target_folder=self.target_folder,
            archive=True)

        # Add a tearDown method to cleanup the logs
        self.register_cleanup(self.cleanup_support_log, log_dir=self.target_folder)

        if not result.passed:
            self.fail("Failed to run daos_server support collect-log command")

        # Extract the collected tar.gz file
        self.extract_logs(self.target_folder + ".tar.gz")

        # Verify server logs files collected for each servers.
        self.validate_server_log_files()

        # Verify the custom log file collected for each servers.
        self.verify_custom_log_data()
