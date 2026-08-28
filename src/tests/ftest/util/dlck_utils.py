"""
  Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from command_utils import RunCommand
from command_utils_base import FormattedParameter


class TestDlck(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Base class for Dlck tests.

    :avocado: recursive
    """

    def get_dlck_command(self, path="", namespace="/run/dlck/*"):
        """Get a DlckCommand object with parameters from the test yaml file.

        Args:
            path (str, optional): path to location of command binary file.  Defaults to "".
            namespace (str, optional): path to yaml parameters. Defaults to "/run/dlck/*".

        Returns:
            DlckCommand: a DlckCommand object with parameters from the test yaml file
        """
        dlck = DlckCommand(path, namespace)
        self.register_cleanup(dlck.cleanup_command)
        dlck.hosts = self.server_managers[0].hosts[0:1]
        dlck.log_dir = self.log_dir
        dlck.get_params(self)
        return dlck


class DlckCommand(RunCommand):
    """Defines a object representing a dlck command."""

    def __init__(self, path="", namespace="/run/dlck/*"):
        """Create a DlckCommand object.

        Args:
            path (str, optional): path to location of command binary file.  Defaults to "".
            namespace (str, optional): path to yaml parameters. Defaults to "/run/dlck/*".
        """
        super().__init__(namespace, "dlck", path)
        self.pool_uuid = FormattedParameter("--file={}", None)
        self.nvme = FormattedParameter("--nvme={}", None)
        self.storage_mount = FormattedParameter("--storage={}", None)
        self.log_dir = FormattedParameter("--log_dir={}", None)
