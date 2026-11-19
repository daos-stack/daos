"""
  Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
import time

import yaml
from ClusterShell.NodeSet import NodeSet
from dlck_utils import TestDlck
from fault_config_utils import FaultInjection
from file_utils import distribute_files
from run_utils import command_as_user, run_remote


class DlckBasicTest(TestDlck):
    """Test class for dlck command line utility.

    :avocado: recursive
    """
    def check_dlck_result(self, result, fault_name):
        """Check that dlck reported the expected error for an injected fault.
        Args:
            result (dict): The result object returned by the dlck command.
            fault_name (str): The name of the injected fault.
        """
        expected_errors = ("DER_INVAL", "DER_NONEXIST", "DER_ID_MISMATCH",
                           "DER_DF_INCOMPT", "DER_DF_INVAL", "WARNING:", "DER_PROTO")
        warning_pattern = r"\b[1-9]\d*\s+warning(?:\(s\)|s?)\b"
        output = f"{result.joined_stdout}\n{result.joined_stderr}"
        expected_error_found = any(error in output for error in expected_errors)
        warning_found = re.search(warning_pattern, output)
        if not (expected_error_found or warning_found):
            return [f"{fault_name}: no expected error found in dlck output: {output}"]
        return []

    def check_daos_engines_stopped(self):
        """Check for DAOS engines up to five times, waiting five seconds between checks."""
        self.log_step("Verify DAOS engines are stopped")
        max_attempts = 5
        for attempt in range(1, max_attempts + 1):
            result = run_remote(self.log, self.hostlist_servers, "/usr/bin/pgrep -a -x daos_engine")
            unchecked_hosts = self.hostlist_servers.copy()
            errors = []
            for data in result.output:
                unchecked_hosts.difference_update(data.hosts)
                if data.returncode == 0:
                    errors.append(
                        f"daos_engine still running on {data.hosts}: {', '.join(data.stdout)}")
                elif data.returncode != 1:
                    errors.append(f"Process check failed on {data.hosts} (exit {data.returncode})")
            if unchecked_hosts:
                errors.append(f"No process check result from {unchecked_hosts}")
            if not errors:
                return
            if attempt == max_attempts:
                self.fail(f"Cannot run dlck after {max_attempts} checks: {'; '.join(errors)}")
            else:
                time.sleep(5)

    def read_nr_hugepages(self, host):
        """Return the number of persistent huge pages on one server."""
        hosts = NodeSet(host)
        if len(hosts) != 1:
            self.fail(f"Expected a single server hostname, got {host!r}")
        result = run_remote(
            self.log, hosts, "sysctl -n vm.nr_hugepages", stderr=True
        )
        if hosts - result.passed_hosts:
            self.fail(f"Could not read vm.nr_hugepages on {host}")
        return int(result.joined_stdout.strip())

    def setup_dlck_env(self):
        """Setup the environment for dlck tests."""
        cmd = 'bash -c "rm -rf /tmp/dpdk* &&' \
              'sync && echo 3 | tee /proc/sys/vm/drop_caches && ' \
              'echo 1 | sudo tee /proc/sys/vm/compact_memory && ' \
              'sysctl -w vm.nr_hugepages=9216"'
        command_root = command_as_user(command=cmd, user="root")
        result = run_remote(log=self.log, hosts=self.hostlist_servers, command=command_root)
        if not result.passed:
            self.fail(f"{command_root} failed on {result.failed_hosts}")

    def revert_nr_hugepages(self, host, nr_hugepages):
        """Restore the number of persistent huge pages and return cleanup errors."""
        cmd = f'sysctl -w vm.nr_hugepages={nr_hugepages}'
        command_root = command_as_user(command=cmd, user="root")
        hosts = NodeSet(host)
        result = run_remote(log=self.log, hosts=hosts, command=command_root)
        if hosts - result.passed_hosts:
            return [f"{command_root} failed on {host}"]
        return []

    def mount_dlck_tmpfs(self, tmpfs_mounts):
        """Mount 124G of memory-backed tmpfs for testing purposes on the remote hosts.

        Args:
            tmpfs_mounts (str): The mount point of the tmpfs to mount.
        """
        cmd = f'sudo bash -c "mkdir -p {tmpfs_mounts} && ' \
              f'mount -t tmpfs -o rw,uid=daos_server,' \
              f'gid=daos_server,mode=0775,size=124G,huge=always tmpfs {tmpfs_mounts}"'
        result = run_remote(log=self.log, hosts=self.hostlist_servers, command=cmd)
        if not result.passed:
            self.fail(f"{cmd} failed on {result.failed_hosts}")

    def unmount_dlck_tmpfs(self, tmpfs_mounts):
        """Unmount the memory-backed tmpfs used for testing on the remote hosts.

        Args:
            tmpfs_mounts (str): The mount point of the tmpfs to unmount.
        """
        cmd = f'umount {tmpfs_mounts}'
        command_root = command_as_user(command=cmd, user="root")
        result = run_remote(log=self.log, hosts=self.hostlist_servers, command=command_root)
        self.log.info("Unmounted dlck tmpfs on remote hosts")
        if not result.passed:
            self.fail(f"{command_root} failed on {result.failed_hosts}")
        return []

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
        dlck.log_dir.value = self.test_env.log_dir
        dlck.storage.value = self.server_managers[0].get_config_value("scm_mount")
        tmpfs_mounts = "/mnt/dlck_test"

        self.log_step("Perform dmg system stop to run dlck command")
        dmg.system_stop()

        if self.server_managers[0].manager.job.using_control_metadata:
            self.log_step("Setup Memory-backed tmpfs for dlck")
            dlck.nvme.value = os.path.join(
                self.server_managers[0].get_config_value("path"), "daos_control", "engine0",
                "daos_nvme.conf")
            orig_hugepages = {host: self.read_nr_hugepages(host) for host in self.hostlist_servers}
            for host, nr_hugepages in orig_hugepages.items():
                dlck.register_cleanup_method(
                    self.revert_nr_hugepages, host=host, nr_hugepages=nr_hugepages)
            self.setup_dlck_env()
            self.mount_dlck_tmpfs(tmpfs_mounts=tmpfs_mounts)
            # Make sure to clean the mounts even if the test hits an error.
            dlck.register_cleanup_method(self.unmount_dlck_tmpfs, tmpfs_mounts=tmpfs_mounts)
            dlck.storage.value = tmpfs_mounts

        self.check_daos_engines_stopped()
        self.log_step("Run dlck command to check the health of the pool and storage")
        dlck.timeout = 180
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
        tmpfs_mounts = "/mnt/dlck_test"
        dmg = self.get_dmg_command()
        pool = self.get_pool()
        dlck = self.get_dlck_command()
        dlck.pool_uuid.value = pool.uuid
        dlck.log_dir.value = self.test_env.log_dir
        dlck.exit_status_exception = False
        dlck.env["D_FI_CONFIG"] = fault_inject_file
        dlck.storage.value = self.server_managers[0].get_config_value("scm_mount")

        self.log_step("Perform dmg system stop to run dlck command")
        dmg.system_stop()

        if self.server_managers[0].manager.job.using_control_metadata:
            self.log_step("Setup Memory-backed tmpfs for dlck")
            dlck.nvme.value = os.path.join(
                self.server_managers[0].get_config_value("path"), "daos_control", "engine0",
                "daos_nvme.conf")
            orig_hugepages = {host: self.read_nr_hugepages(host) for host in self.hostlist_servers}
            for host, nr_hugepages in orig_hugepages.items():
                dlck.register_cleanup_method(
                    self.revert_nr_hugepages, host=host, nr_hugepages=nr_hugepages)
            self.setup_dlck_env()
            self.mount_dlck_tmpfs(tmpfs_mounts=tmpfs_mounts)
            # Make sure to clean the mounts even if the test hits an error.
            dlck.register_cleanup_method(self.unmount_dlck_tmpfs, tmpfs_mounts=tmpfs_mounts)
            dlck.storage.value = tmpfs_mounts

        for test_fault in fault_list:
            if test_fault == "DAOS_FAULT_POOL_OPEN_BIO" and \
               not self.server_managers[0].manager.job.using_control_metadata:
                self.log.info("Skipping test fault %s as control metadata is not used", test_fault)
                continue
            fault_config = {
                'fault_config': [
                    {key: str(value) for key, value in faults_dict[test_fault].items()}]}
            with open(fault_inject_file, 'w') as f:
                yaml.dump(fault_config, f)
            self.log.info("Reading the updated fault injection file contents")

            with open(fault_inject_file, 'r') as f:
                file_data = f.read()
                self.log.info("\n %s", file_data)
            distribute_files(self.log, self.hostlist_servers, fault_inject_file,
                             fault_inject_file)
            self.check_daos_engines_stopped()
            self.log_step("Run dlck command after injecting fault")
            dlck.timeout = 180
            result = dlck.run()
            errors.extend(self.check_dlck_result(result, test_fault))

        self.log_step("Perform dmg system start after running dlck command")
        dmg.system_start()

        if errors:
            self.fail(f"dlck basic test failed with errors: {errors}")
        self.log.info("dlck basic fault test passed with no errors")
