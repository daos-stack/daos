"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP
  (C) Copyright 2025 Google LLC
  (C) Copyright 2025 Enakta Labs Ltd

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from command_utils import ExecutableCommand
from command_utils_base import FormattedParameter
from run_utils import run_remote


class DirectoryTreeCommand(ExecutableCommand):
    """Class defining the DirectoryTreeCommand object."""

    def __init__(self, hosts, namespace="/run/directory_tree/*"):
        """Create a DirectoryTreeCommand object.

        Args:
            hosts (NodeSet): hosts on which to run the command
            namespace (str): command namespace. Defaults to /run/directory_tree/*
        """
        path = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
        super().__init__(namespace, "directory_tree.py", path)

        # directory_tree.py options
        self.path = FormattedParameter("--path {}")
        self.height = FormattedParameter("--height {}")
        self.subdirs = FormattedParameter("--subdirs {}")
        self.files = FormattedParameter("--files {}")
        self.needles = FormattedParameter("--needles {}")
        self.prefix = FormattedParameter("--prefix {}")
        self.file_size_min = FormattedParameter("--file-size-min {}")
        self.file_size_max = FormattedParameter("--file-size-max {}")

        # run options
        self.hosts = hosts.copy()
        self.timeout = 180

    def run(self):
        # pylint: disable=arguments-differ
        """Run the command.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status
        """
        self.log.info('Running directory_tree.py on %s', str(self.hosts))
        return run_remote(self.log, self.hosts, self.with_exports, timeout=self.timeout)
