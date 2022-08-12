"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from command_utils_base import PositionalParameter
from command_utils import ExecutableCommand
from general_utils import run_pcmd


class DdbCommandBase(ExecutableCommand):
    """Defines the basic structures of ddb command."""

    def __init__(self, server_host, path, verbose=True, timeout=None, sudo=True):
        """Defines the parameters for ddb.

        Args:
            server_host (NodeSet): Server host to run the command.
            path (str): path to the ddb command.
            verbose (bool, optional): Display command output when run_pcmd is called.
                Defaults to True.
            timeout (int, optional): Command timeout (sec) used in run_pcmd. Defaults to
                None.
            sudo (bool, optional): Whether to run ddb with sudo. Defaults to True.
        """
        super().__init__("/run/ddb/*", "ddb", path)

        # We need to run with sudo.
        self.sudo = sudo

        self.host = server_host

        # Write mode that's necessary for the commands that alters the data such as load.
        self.write_mode = PositionalParameter(position=1, default=False, str_format="-w")

        # Run ddb with single mode with -R. i.e., non-interactive mode.
        self.run_cmd = PositionalParameter(position=2, default=True, str_format="-R")

        # Command to run on the VOS file that contains container, object info, etc.
        # Specify double quotes in str_format because the command needs to be wrapped
        # with them.
        self.single_command = PositionalParameter(position=3, str_format="\"{}\"")

        # VOS file path.
        self.vos_path = PositionalParameter(position=4)

        # Members needed for run_pcmd().
        self.verbose = verbose
        self.timeout = timeout

    def run(self):
        """Run the command.

        Returns:
            list: A list of dictionaries with each entry containing output, exit status,
                and interrupted status common to each group of hosts.

        """
        return run_pcmd(
            hosts=self.host, command=self.__str__(), verbose=self.verbose,
            timeout=self.timeout)
