"""
  Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from command_utils import ExecutableCommand
from command_utils_base import FormattedParameter
from run_utils import run_remote


class DlckCommand(ExecutableCommand):
    """Defines the basic structures of dlck command."""

    def __init__(self, server_host, path, pool_uuid=None, nvme_conf=None, storage_mount=None,
                 verbose=True, timeout=None, sudo=True):
        """Constructor that sets the common variables for sub-commands.

        Args:
            server_host (NodeSet): Server host to run the command.
            path (str): path to the dlck command.
            pool_uuid (str, optional): Pool UUID. Defaults to None.
            nvme_conf (str, optional): NVMe config file path. Defaults to None.
            storage_mount (str, optional): Storage mount point. Defaults to None.
            verbose (bool, optional): Display command output in run.
                Defaults to True.
            timeout (int, optional): Command timeout (sec) used in run. Defaults to
                None.
            sudo (bool, optional): Whether to run dlck with sudo. Defaults to True.
        """
        super().__init__("/run/dlck/*", "dlck", path)
        # Get the fault injection file path and set environment string for the command.
        fault_inject_file = os.getenv("D_FI_CONFIG", "None set for now")
        # Pass environment variable string
        self.env_str = ""
        self.env_str = "D_FI_CONFIG={} ".format(fault_inject_file)
        # We need to run with sudo -E -n
        self.dlck_sudo = sudo

        self.host = server_host

        # Members needed for run().
        self.verbose = verbose
        self.timeout = timeout

        # Pool UUID. (--file pool_uuid[,target_id])
        if pool_uuid:
            self.pool_uuid = FormattedParameter("--file={}", pool_uuid)

        # NVMe config file path. (--nvme nvme_conf)
        if nvme_conf:
            self.nvme = FormattedParameter("--nvme={}", nvme_conf)

        # Storage mount point. (--storage storage_mount)
        if storage_mount:
            self.storage_mount = FormattedParameter("--storage={}", storage_mount)

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters
        """
        value = super().__str__()
        if self.dlck_sudo:
            value = " ".join(["sudo -E -n", value])
        return value

        return value

    def run(self):
        """Run the dlck command.
        Args:
            host (NodeSet): Host(s) on which to run the command.
            command (str): Environment Variable string + dlck sub-command to run.
            verbose (bool, optional): Display command output in run.
                Defaults to True.
            timeout (int, optional): Command timeout (sec) used in run. Defaults to
                None.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status
        """
        return run_remote(
            self.log, self.host, command=self.env_str + str(self), verbose=self.verbose,
            timeout=self.timeout)
