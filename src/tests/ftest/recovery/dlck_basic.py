"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from dlck_utils import DlckCommand

class DlckBasicTest(TestWithServers):
    """Test class for dlck command line utility.

    :avocado: recursive
    """
    def test_dlck_basic_test(self):
        """Basic Test: Run 'dlck' command

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery
        :avocado: tags=DlckBasicTest,dlck_cmd,test_dlck_basic
        """
        errors = []
        dmg = self.get_dmg_command()
        self.add_pool()
        pool_uuids = dmg.get_pool_list_uuids(no_query=True)
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        if self.server_managers[0].manager.job.using_control_metadata:
          log_dir = os.path.dirname(self.server_managers[0].get_config_value("log_file"))
          control_metadata_dir = os.path.join(log_dir, "control_metadata")
          nvme_conf=os.path.join(control_metadata_dir, "daos_nvme.conf")
        dmg.system_stop()
        host = self.server_managers[0].hosts[0:1]
        if self.server_managers[0].manager.job.using_control_metadata:
          dlck_cmd = DlckCommand(host, self.bin, pool_uuids[0], nvme_conf=nvme_conf,
                                storage_mount=scm_mount)
        else:
          dlck_cmd = DlckCommand(host, self.bin, pool_uuids[0], storage_mount=scm_mount)
        result = dlck_cmd.run()
        if not result.passed:
          errors.append(f"dlck failed on {result.failed_hosts}")
        self.log.info("dlck basic test output:\n%s", result)
        dmg.system_start()
        if errors:
            self.fail("Errors detected:\n{}".format("\n".join(errors)))
        self.log.info("dlck basic test passed with no errors")

