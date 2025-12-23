"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from command_utils_base import CommandWithParameters, FormattedParameter
from run_utils import run_remote

  
class DlckCommand(CommandWithParameters):
    """Defines the basic structures of dlck command."""

    def __init__(self, server_host, path, pool_uuid=None, nvme_conf=None, storage_mount=None,
                 verbose=True, timeout=None, sudo=True, env_str=None):
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
            env_str (str, optional): Environment variable string to prepend to command.
        
        """
        super().__init__("/run/dlck/*", "dlck", path)
        # Pass environment variable string
        self.env_str = ""
        if env_str is not None:
           self.env_str = str(env_str)        


        # We need to run with sudo.
        self.sudo = sudo

        self.host = server_host

        # Members needed for run().
        self.verbose = verbose
        self.timeout = timeout

        # Pool UUID. (--file pool_uuid[,target_id])
        if pool_uuid:
            self.pool_uuid = FormattedParameter("--file={}", pool_uuid)

        # NVMe config file path. (--nvme_cont nvme_conf_path)
        if nvme_conf:
            self.nvme_conf = FormattedParameter("--nvme={}", nvme_conf)

        # Storage mount point. (--storage storage_mount)
        if storage_mount:
            self.storage_mount = FormattedParameter("--storage={}", storage_mount)

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        value = super().__str__()
        if self.sudo:
            value = " ".join(["sudo -E -n", value])
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
