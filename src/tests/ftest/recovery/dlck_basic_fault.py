"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from dlck_utils import DlckCommand
from run_utils import run_remote
from file_utils import distribute_files
from fault_config_utils import FaultInjection

class DlckBasicFaultTest(TestWithServers):
    """Test class for dlck command line utility.

    :avocado: recursive
    """
    def test_dlck_basic_fault_test(self):
        """Basic Fault Test: Run 'dlck' injecting basic faults.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery
        :avocado: tags=DlckBasicFaultTest,dlck_cmd,test_dlck_basic_fault
        """
        errors = []
        faults_dict = {}
        faults_object = FaultInjection()
        faults_dict = faults_object.get_faults_dict()
        fault_list = self.params.get("fault_list", '/run/dlck_test_additional_faults/*')
        dmg = self.get_dmg_command()
        self.add_pool()
        pool_uuids = dmg.get_pool_list_uuids(no_query=True)
        scm_mount = self.server_managers[0].get_config_value("scm_mount")
        if self.server_managers[0].manager.job.using_control_metadata:
          log_dir = os.path.dirname(self.server_managers[0].get_config_value("log_file"))
          control_metadata_dir = os.path.join(log_dir, "control_metadata")
          nvme_conf=os.path.join(control_metadata_dir, "daos_nvme.conf")
        fault_inject_file = os.getenv("D_FI_CONFIG", "None set for now")
        if fault_inject_file == "None set for now":
          self.fail("D_FI_CONFIG environment variable not set, cannot run fault injection test")
        env_str = "D_FI_CONFIG={} ".format(fault_inject_file)
        self.log.info("Fault injection file contents")
        cmd = "cat {}".format(fault_inject_file)        
        host = self.server_managers[0].hosts[0:1]
        run_remote(self.log, self.hostlist_clients[0], cmd, timeout=30)
        dmg.system_stop()
        # Run the testing with the first fault which is injected at the beginning of the test.
        if self.server_managers[0].manager.job.using_control_metadata:
          dlck_cmd = DlckCommand(host, self.bin, pool_uuids[0], nvme_conf=nvme_conf,
                                storage_mount=scm_mount, env_str=env_str)
        else:
          dlck_cmd = DlckCommand(host, self.bin, pool_uuids[0], storage_mount=scm_mount, env_str=env_str)
        result = dlck_cmd.run()
        if not result.passed:
          errors.append(f"dlck failed on {result.failed_hosts}")
        self.log.info("dlck basic test output:\n%s", result)
        # Now, run the other fault injection flags without rebooting or creating any new pools.
        # Rebooting the servers or creating the new pools will result in injectung fault in the wrong
        # test code. Fault injections should done only for the dlck alone.
        for test_fault in fault_list:
          with open(fault_inject_file, 'w') as f:
            f.write(f"fault_config:\n")
            count = 0
            for key, value in faults_dict[test_fault].items():
                if count == 0:
                  f.write(f"- {key}: \'{value}\'\n")
                else:
                  f.write(f"  {key}: \'{value}\'\n")  
                count += 1           
          f.close()
          self.log.info("Reading the updated fault injection file contents")
          with open(fault_inject_file, 'r') as f:
            file_data = f.read()
            self.log.info("\n%s", file_data)
          f.close()
          distribute_files(self.log, self.hostlist_servers, fault_inject_file, fault_inject_file)
          #faults_object.copy_fault_files(self.log, self.hostlist_servers)
          if self.server_managers[0].manager.job.using_control_metadata:
            dlck_cmd = DlckCommand(host, self.bin, pool_uuids[0], nvme_conf=nvme_conf,
                                  storage_mount=scm_mount, env_str=env_str)
          else:
            dlck_cmd = DlckCommand(host, self.bin, pool_uuids[0], storage_mount=scm_mount, env_str=env_str)
          result = dlck_cmd.run()
          if not result.passed:
            errors.append(f"dlck failed on {result.failed_hosts}")
          self.log.info("dlck basic test output:\n%s", result)
        dmg.system_start()
        if not errors:
            self.fail("No Errors detected:\n{}".format("\n".join(errors)))
