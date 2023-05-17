"""
(C) Copyright 2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from datetime import datetime

from control_test_base import ControlTestBase
from run_utils import run_remote


class SupportTestBase(ControlTestBase):
    """Class for Support log collection """

    def __init__(self, *args, **kwargs):
        """Initialize a SupportTestBase object."""
        super().__init__(*args, **kwargs)
        self.target_folder = None
        self.custom_log_dir = None
        self.custom_log_file = None
        self.custom_log_data = None
        self.log_hosts = None
        self.extract_dir = os.path.join(self.base_test_dir, "extracted_support_logs")

    def create_custom_log(self, folder_name):
        """Create custom log directory with custom data file on each servers.

        Args:
            folder_name (str): Name of the custom folder
        """
        server_custom_log = self.base_test_dir
        self.custom_log_dir = os.path.join(server_custom_log, folder_name)
        self.custom_log_file = os.path.join(self.custom_log_dir, "Custom_File")
        self.target_folder = os.path.join(self.base_test_dir, "DAOS_Support")

        # make the custom log dir on node (clients or servers)
        mkdir_cmd = "mkdir -p {}".format(self.custom_log_dir)
        result = run_remote(self.log, self.log_hosts, mkdir_cmd)
        if not result.passed:
            self.fail("Failed to create the custom log dir {} ".format(result))

        # Get date-time object containing current date and time
        now = datetime.now()
        self.custom_log_data = now.strftime("%d/%m/%Y %H:%M:%S")

        # Create the custom log file on node (clients or servers)
        create_file = " echo \'{}\' > {}".format(self.custom_log_data, self.custom_log_file)
        result = run_remote(self.log, self.log_hosts, create_file)
        if not result.passed:
            self.fail("Failed to create the custom log file {} ".format(result))

    def verify_custom_log_data(self):
        """Verify custom log files is collected and part of archive.

        """
        read_filedata = "find {}  -name {} | xargs cat".format(
            self.extract_dir, os.path.basename(self.custom_log_file))

        result = run_remote(self.log, self.log_hosts, read_filedata)
        if not result.passed:
            self.fail("Failed to read the custom log file {} ".format(result))

        for output in result.all_stdout.values():
            if self.custom_log_data not in output:
                self.fail("Expected custom_log_data {} not found in log file {}"
                          .format(self.custom_log_data, self.custom_log_file))

    def extract_logs(self, tar_gz_filename):
        """Extract the logs files which are in collected archive.

        Args:
            tar_gz_filename (str): Log archive File name

        Raises:
            Test Failure: If unable to run any command.

        """
        # Create the new extract directory
        cmd = "mkdir -p {}".format(self.extract_dir)
        result = run_remote(self.log, self.log_hosts, cmd)
        if not result.passed:
            self.fail("cmd {} failed, result:{}".format(cmd, result))

        # Extract The tar.gz file to newly created directory
        cmd = "tar -xf {} -C {}".format(tar_gz_filename, self.extract_dir)
        result = run_remote(self.log, self.log_hosts, cmd)
        if not result.passed:
            self.fail("Failed to extract the {} file, result:{}".format(tar_gz_filename, result))

    def validate_server_log_files(self):
        """Verify all the server logs files are collected and part of archive.

        Raises:
            Test Failure: If unable to run any command.

        """
        log_files = []
        helper_log_file = self.server_managers[0].get_config_value("helper_log_file")
        log_files.append(os.path.basename(helper_log_file))
        control_log_file = self.server_managers[0].get_config_value("control_log_file")
        log_files.append(os.path.basename(control_log_file))
        log_files.append(self.params.get("log_file", "/run/server_config/engines/0/*"))
        log_files.append(self.params.get("log_file", "/run/server_config/engines/1/*"))

        # Verify server log files are collected.
        for log_file in log_files:
            list_file = "ls -lsaRt {} | grep {}".format(self.extract_dir, log_file)
            result = run_remote(self.log, self.log_hosts, list_file)
            if not result.passed:
                self.fail("Failed to list the {} file from extracted folder{}".format(
                          result, self.extract_dir))

    def cleanup_support_log(self, log_dir):
        """ Test cleanup to remove the temporary directory

        Args:
            log_dir (str): Name of the log directory to be removed

        Returns:
            list: a list of any errors detected when deleting the logs

        """
        error_list = []
        # Remove the log and extract directory
        folders = [log_dir, self.extract_dir]
        for folder in folders:
            delete_cmd = "sudo rm -rf {}*".format(folder)
            result = run_remote(self.log, self.log_hosts, delete_cmd)
            if not result.passed:
                error_list.append(
                    "Failed to delete folder {} with result:{}".format(folder, result))

        return error_list
