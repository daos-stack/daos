"""
  Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from dlck_utils import TestDlck
from test_utils_pool import add_pool


class DlckBasicTest(TestDlck):
    """Test class for dlck command line utility.

    :avocado: recursive
    """
    def test_dlck_basic(self):
        """Basic Test: Run 'dlck' command

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,dlck_cmd
        :avocado: tags=DlckBasicTest,test_dlck_basic
        """
        errors = []
        dmg = self.get_dmg_command()
        self.log_step("Create a pool to run dlck")
        pool = add_pool(self)
        dlck = self.get_dlck_command()
        dlck.pool_uuid.value = pool.uuid
        dlck.log_dir = self.test_env.log_dir
        dlck.storage_mount.value = self.server_managers[0].get_config_value("scm_mount")
        if self.server_managers[0].manager.job.using_control_metadata:
            dlck.nvme.value = os.path.join(
                self.server_managers[0].get_config_value("path"), "daos_control", "engine0",
                "daos_nvme.conf")
        self.log_step("Perform dmg system stop to run dlck command")
        dmg.system_stop()
        self.log_step("Run dlck command to check the health of the pool and storage")
        result = dlck.run()
        dmg.system_start()
        if not result.passed:
            errors.append(f"dlck failed on {result.failed_hosts}")
            self.fail(f"dlck basic test failed with errors: {errors}")
        self.log.info("dlck basic test passed with no errors")
