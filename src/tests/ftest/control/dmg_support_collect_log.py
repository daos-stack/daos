"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from support_test_base import SupportTestBase


class DmgSupportCollectLogTest(SupportTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:Verify the dmg support collect-log command.

    :avocado: recursive
    """

    def test_dmg_support_collect_log(self):
        """JIRA ID: DAOS-10625

        Test Description:
            Test that dmg support collect-log command completes successfully.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=basic,control,support,dmg
        :avocado: tags=DmgSupportCollectLogTest,test_dmg_support_collect_log
        """
        self.log_hosts = self.hostlist_servers
        # Create the custom log data which will be collected via support collect-log,
        # Later verify the dame data file is archived as part of collection.
        self.create_custom_log("Support_Custom_Dir")

        # Run dmg support collect-log with --extra-logs-dir,
        # copy logs to folder with command option --target-folder
        # Enable archive mode.
        result = self.dmg.support_collect_log(extra_logs_dir=self.custom_log_dir,
                                              target_folder=self.target_folder,
                                              archive=True)
        status = result["status"]
        self.assertEqual(status, 0, "bad return status from dmg support collect-log")

        # Extract the collected tar.gz file
        self.extract_logs(self.target_folder + ".tar.gz")

        # Verify server logs file collected.
        self.validate_server_log_files()

        # Verify the custom log file collected.
        self.verify_custom_log_data()

        # Clean up the log file created during test execution
        self.cleanup_support_log(self.target_folder)
