"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from server_utils_base import DaosServerCommand


class DaosServerCommandRunner(DaosServerCommand):
    """"""

    def __init__(self, path):
        """Create a daos_server Command object.

        Args:
            path (str): path to the daos_server command
        """
        super().__init__(path)

        self.debug.value = False
        self.json_logs.value = True

    def recover(self, force=False):
        """Call daos_server recover.

        Args:
            force (bool, optional): Don't prompt for confirmation. Defaults to False.

        Returns:
            CmdResult: an avocado CmdResult object containing the daos_server command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the daos_server recover command fails.

        """
        return self._get_result(["recover"], force=force)

    def restore(self, force=False, path=None):
        """Call daos_server restore.

        Args:
            force (bool, optional): Don't prompt for confirmation. Defaults to False.
            path (str, optional): Path to snapshot file. Defaults to None.

        Returns:
            CmdResult: an avocado CmdResult object containing the daos_server command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the daos_server restore command fails.

        """
        return self._get_result(["restore"], force=force, path=path)

    def status(self):
        """Call daos_server status.

        Returns:
            CmdResult: an avocado CmdResult object containing the daos_server command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the daos_server status command fails.

        """
        return self._get_result(["status"])

    def version(self):
        """Call daos_server version.

        Returns:
            CmdResult: an avocado CmdResult object containing the daos_server command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the daos_server version command fails.

        """
        return self._get_result(["version"])
