"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from env_modules import show_avail, get_module_list
from general_utils import run_command


class CommandFailure(Exception):
    """Base exception for this module."""


class MPILoadError(Exception):
    """Exception raised when loading an MPI module fails."""

    def __init__(self, module):
        """Create an MPILoadError object.

        Args:
            module (str): the name of the module
            message (str): error explanation
        """
        message = "Failed to load an {0} module from the list {3}.\n" \
                  "Available modules:\n{1}\n" \
                  "Installed *{0}* RPMs:\n{2}\nEnvironment:\n{4}".format(
                      module, show_avail(),
                      "\n".join(list(filter(
                          lambda x: module in x,
                          run_command("rpm -qa").stdout_text.split("\n")))),
                      ' '.join(get_module_list(module)),
                      "\n".join([f"{k}: {v}" for k, v in sorted(os.environ.items())]))
        super().__init__(message)
