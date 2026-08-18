"""
  Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from dlck_utils import DlckCommand
from test_utils_pool import add_pool


class DlckBasicTest(TestWithServers):
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
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        host = self.server_managers[0].hosts[0:1]
        if self.server_managers[0].manager.job.using_control_metadata:
            log_dir = os.path.dirname(self.server_managers[0].get_config_value("log_file"))
            control_metadata_dir = os.path.join(log_dir, "control_metadata")
            daos_control_dir = os.path.join(control_metadata_dir, "daos_control")
            engine_path_dir = os.path.join(daos_control_dir, "engine0")
            nvme_conf = os.path.join(engine_path_dir, "daos_nvme.conf")
            dlck_cmd = DlckCommand(host, self.bin, pool.uuid, nvme_conf=nvme_conf,
                                   storage_mount=scm_mount)
        else:
            dlck_cmd = DlckCommand(host, self.bin, pool.uuid, storage_mount=scm_mount)
        self.log_step("Perform dmg system stop to run dlck command")
        dmg.system_stop()
        self.log_step("Run dlck command to check the health of the pool and storage")
        result = dlck_cmd.run()
        if not result.passed:
            errors.append(f"dlck failed on {result.failed_hosts}")
        dmg.system_start()
        if errors:
            self.fail(f"dlck basic test failed with errors: {errors}")
        self.log.info("dlck basic test passed with no errors")
