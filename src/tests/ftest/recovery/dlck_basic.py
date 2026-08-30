"""
  Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re

from dlck_test_base import DlckTestBase
from fault_config_utils import FaultInjection
from file_utils import distribute_files


class DlckBasicTest(DlckTestBase):
    """Test class for dlck command line utility.

    :avocado: recursive
    """
    def check_dlck_result(self, result, fault_name):
        """Check that dlck reported the expected error for an injected fault."""
        """
        Args:
            result (dict): The result object returned by the dlck command.
            fault_name (str): The name of the injected fault.
        """
        expected_errors = ("DER_INVAL", "DER_NONEXIST", "DER_ID_MISMATCH",
                           "DER_DF_INCOMPT", "DER_DF_INVAL", "WARNING:")
        warning_pattern = r"\b\d+\s+warning(?:\(s\)|s?)\b"
        output = f"{result.joined_stdout}\n{result.joined_stderr}"
        expected_error_found = any(error in output for error in expected_errors)
        warning_found = re.search(warning_pattern, output)
        if not (expected_error_found or warning_found):
            return f"{fault_name}: no expected error found in dlck output: {output}"
        return None

    def test_dlck_basic(self):
        """Basic Test: Run 'dlck' command

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,dlck_cmd
        :avocado: tags=DlckBasicTest,test_dlck_basic
        """
        dmg = self.get_dmg_command()
        self.log_step("Create a pool to run dlck")
        pool = self.get_pool()
        dlck = self.get_dlck_command()
        dlck.pool_uuid.value = pool.uuid
        dlck.log_dir = self.test_env.log_dir
        dlck.run_user = 'daos_server'
        dlck.storage.value = self.server_managers[0].get_config_value("scm_mount")
        if self.server_managers[0].manager.job.using_control_metadata:
            dlck.nvme.value = os.path.join(
                self.server_managers[0].get_config_value("path"), "daos_control", "engine0",
                "daos_nvme.conf")

        self.log_step("Perform dmg system stop to run dlck command")
        dmg.system_stop()

        self.log_step("Run dlck command to check the health of the pool and storage")
        result = dlck.run()

        self.log_step("Perform dmg system start after running dlck command")
        dmg.system_start()
        if not result.passed:
            self.fail(f"dlck failed on {result.failed_hosts}")
        self.log.info("dlck basic test passed with no errors")

    def test_dlck_basic_fault(self):
        """Basic Fault Test: Run 'dlck' injecting basic faults.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,dlck_cmd,faults
        :avocado: tags=DlckBasicTest,test_dlck_basic_fault
        """
        errors = []
        faults_object = FaultInjection()
        faults_dict = faults_object.get_faults_dict()
        fault_list = self.params.get("fault_list", '/run/dlck_test_faults/*')
        self.log.info("Test log dir %s", self.test_env.log_dir)
        fault_inject_file = os.path.join(self.test_env.log_dir, "fi.yaml")
        self.log.info("Fault injection file: %s", fault_inject_file)
        self.log.info("Faults: %s", fault_list)
        self.log.info("Faults dict: %s", faults_dict)
        dmg = self.get_dmg_command()
        pool = self.get_pool()
        dlck = self.get_dlck_command()
        dlck.pool_uuid.value = pool.uuid
        dlck.log_dir = self.test_env.log_dir
        dlck.run_user = 'daos_server'
        dlck.exit_status_exception = False
        dlck.env["D_FI_CONFIG"] = fault_inject_file
        dlck.storage.value = self.server_managers[0].get_config_value("scm_mount")
        if self.server_managers[0].manager.job.using_control_metadata:
            dlck.nvme.value = os.path.join(
                self.server_managers[0].get_config_value("path"), "daos_control", "engine0",
                "daos_nvme.conf")

        self.log_step("Perform dmg system stop to run dlck command")
        dmg.system_stop()

        for test_fault in fault_list:
            with open(fault_inject_file, 'w') as f:
                f.write("fault_config:\n")
                for count, (key, value) in enumerate(faults_dict[test_fault].items()):
                    if count == 0:
                        f.write(f"- {key}: \'{value}\'\n")
                    else:
                        f.write(f"  {key}: \'{value}\'\n")
            self.log.info("Reading the updated fault injection file contents")
            with open(fault_inject_file, 'r') as f:
                file_data = f.read()
                self.log.info("\n %s", file_data)
            distribute_files(self.log, self.hostlist_servers, fault_inject_file,
                             fault_inject_file)
            self.log_step("Run dlck command after injecting fault")
            result = dlck.run()
            error = self.check_dlck_result(result, test_fault)
            if error:
                errors.append(error)

        self.log_step("Perform dmg system start after running dlck command")
        dmg.system_start()

        if errors:
            self.fail(f"dlck basic test failed with errors: {errors}")
        self.log.info("dlck basic fault test passed with no errors")
