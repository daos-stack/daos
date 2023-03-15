"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from general_utils import run_pcmd
from control_test_base import ControlTestBase


class DmgSupportCollectLogTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Verify the support collect-log function of the dmg tool.
    :avocado: recursive
    """
    def test_dmg_support_collect_log(self):
        """JIRA ID: DAOS-10625
        Test Description: Test that support collect-log command completes successfully.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=basic,control,dmg
        :avocado: tags=test_dmg_support_collect_log_SAMIR
        """
        result = self.dmg.support_collect_log()
        status = result["status"]
        self.assertEqual(status, 0, "bad return status")

    def test_dmg_support_collect_log_with_stop(self):
        """JIRA ID: DAOS-10625
        Test Description: Test that support collect-log --stop-on-error command completes successfully.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=basic,control,dmg
        :avocado: tags=test_dmg_support_collect_log_SAMIR
        """
        result = self.dmg.support_collect_log(stop=True)
        status = result["status"]
        self.assertEqual(status, 0, "bad return status")

    def test_dmg_support_collect_log_with_custom(self):
        """JIRA ID: DAOS-10625
        Test Description: Test that support collect-log command with custom logs completes successfully.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=basic,control,dmg
        :avocado: tags=test_dmg_support_collect_log_SAMIR
        """
        # Create the custom folder/file on each servers
        server_custom_log = os.environ['AVOCADO_TESTS_COMMON_TMPDIR']
        custom_log_dir = os.path.join(server_custom_log, "Custom_Dir")
        custom_log_file = os.path.join(server_custom_log, "Custom_Dir", "Custom_File")
        target_folder = os.path.join("/tmp", "Target_Dir")

        # make the custom log dir on all servers
        mkdir_cmd = "mkdir -p {}".format(custom_log_dir)
        results = run_pcmd(hosts=self.hostlist_servers, command=mkdir_cmd)
        for result in results:
          if result["exit_status"] != 0:
            self.fail("Failed to create the custom log dir {} ".format(result))

        # make the custom log file on all servers
        create_file = " echo \"Test Log File\" > {}".format(custom_log_file)
        results = run_pcmd(hosts=self.hostlist_servers, command=create_file)
        for result in results:
          if result["exit_status"] != 0:
            self.fail("Failed to create the custom log file {} ".format(result))

        # Run dmg support collect-log with --extra-logs-dir
        # Copy the log to non default folder with dmg command option --target-folder
        result = self.dmg.support_collect_log(custom_logs=custom_log_dir, target_folder=target_folder)
        status = result["status"]
        self.assertEqual(status, 0, "bad return status")

        # Verify the custom log file collected on each servers.
        read_filedata = "cat {}/*/ExtraLogs/{}".format(target_folder, os.path.basename(custom_log_file))
        results = run_pcmd(hosts=self.hostlist_servers, command=read_filedata)
        for result in results:
          if result["exit_status"] != 0:
            self.fail("Failed to read the custom log file {} ".format(result))

          if "Test Log File" not in result["stdout"]:
            self.fail("Test Log File not found in log file instead {}".format(result["stdout"]))

    def test_dmg_support_collect_log_with_archive(self):
        """JIRA ID: DAOS-10625
        Test Description: Test that support collect-log command with archive completes successfully.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=basic,control,dmg
        :avocado: tags=test_dmg_support_collect_log
        """
        result = self.dmg.support_collect_log(archive=True)
        status = result["status"]
        self.assertEqual(status, 0, "bad return status")
        log_files = []
        helper_log_file = self.server_managers[0].get_config_value("helper_log_file")
        log_files.append(os.path.basename(helper_log_file))
        control_log_file = self.server_managers[0].get_config_value("control_log_file")
        log_files.append(os.path.basename(control_log_file))
        server_log_file = self.server_managers[0].get_config_value("log_file")
        log_files.append(os.path.basename(server_log_file))

        # Verify log file is archived as part of collect-log.
        for log_file in log_files:
          list_file = "tar -ztvf /tmp/daos_support_server_logs.tar.gz | grep {}".format(log_file)
          results = run_pcmd(hosts=self.hostlist_servers, command=list_file)

          for result in results:
            if result["exit_status"] != 0:
              self.fail("Failed to list the {} file from tar.gz".format(result))
