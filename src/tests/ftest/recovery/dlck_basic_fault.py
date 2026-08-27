"""
  Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re

from dlck_utils import TestDlck
from fault_config_utils import FaultInjection
from file_utils import distribute_files
from run_utils import run_remote
from test_utils_pool import add_pool


class DlckBasicFaultTest(TestDlck):
    """Test class for dlck command line utility.

    :avocado: recursive
    """
    def check_dlck_result(self, result, fault_name):
        """Check that dlck reports the expected error for an injected fault."""
        expected_errors = ("DER_INVAL", "DER_NONEXIST", "DER_ID_MISMATCH",
                           "DER_DF_INCOMPT", "DER_DF_INVAL", "WARNING:")
        warning_pattern = r"\b\d+\s+warning(?:\(s\)|s?)\b"
        output = f"{result.joined_stdout}\n{result.joined_stderr}"
        expected_error_found = any(error in output for error in expected_errors)
        warning_found = re.search(warning_pattern, output)
        if not (expected_error_found or warning_found):
            return f"{fault_name}: no expected error found in dlck output: {output}"
        return None

    def test_dlck_basic_fault(self):
        """Basic Fault Test: Run 'dlck' injecting basic faults.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,dlck_cmd
        :avocado: tags=DlckBasicFaultTest,test_dlck_basic_fault
        """
        errors = []
        faults_object = FaultInjection()
        faults_dict = faults_object.get_faults_dict()
        fault_list = self.params.get("fault_list", '/run/dlck_test_additional_faults/*')
        dmg = self.get_dmg_command()
        pool = add_pool(self)
        dlck = self.get_dlck_command()
        dlck.pool_uuid.value = pool.uuid
        dlck.log_dir = self.test_env.log_dir
        dlck.storage_mount.value = self.server_managers[0].get_config_value("scm_mount")
        fault_inject_file = os.getenv("D_FI_CONFIG", "None set for now")
        if fault_inject_file == "None set for now":
            self.fail("D_FI_CONFIG environment variable not set")
        self.log.info("Fault injection file contents")
        cmd = f"cat {fault_inject_file}"
        self.log_step("Run the command to read the fault injection file contents")
        run_remote(self.log, self.hostlist_clients[0], cmd, timeout=30)
        # Run the testing with the first fault which is injected at the beginning of the test.
        if self.server_managers[0].manager.job.using_control_metadata:
            dlck.nvme.value = os.path.join(
                self.server_managers[0].get_config_value("path"), "daos_control", "engine0",
                "daos_nvme.conf")
        self.log_step("Perform dmg system stop to run dlck command")
        dmg.system_stop()
        self.log_step("Run dlck command after injecting the first fault")
        result = dlck.run()
        error = self.check_dlck_result(result, "Initial fault")
        if error:
            errors.append(error)
        # Now, run the other fault injection flags without rebooting or creating any new pools.
        # Rebooting the servers or creating the new pools will result in injecting fault in
        # the wrong test code. Fault injections should be done only for the dlck alone.
        for test_fault in fault_list:
            with open(fault_inject_file, 'w') as f:
                f.write("fault_config:\n")
                count = 0
                for key, value in faults_dict[test_fault].items():
                    if count == 0:
                        f.write(f"- {key}: \'{value}\'\n")
                    else:
                        f.write(f"  {key}: \'{value}\'\n")
                    count += 1
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
        self.log_step("Run the dmg start command")
        dmg.system_start()

        if errors:
            self.fail(f"dlck basic test failed with errors: {errors}")
        self.log.info("dlck basic fault test passed with no errors")
