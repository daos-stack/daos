"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from command_utils_base import PositionalParameter
from command_utils import ExecutableCommand


class DdbCommandBase(ExecutableCommand):
    """Defines the basic structures of ddb command."""

    def __init__(self, path):
        """Defines the parameters for ddb.

        Args:
            path (str): path to the ddb command
        """
        super().__init__("/run/ddb/*", "ddb", path)

        # We need to run with sudo.
        self.sudo = True

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
