#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from command_utils import ExecutableCommand
from command_utils_base import FormattedParameter


class CartSelfTestCommand(ExecutableCommand):
    """Defines a CaRT self test command."""

    def __init__(self, path=""):
        """Create a SelfTest object.

        Uses Avocado's utils.process module to run self_test with parameters.

        Args:
            path (str, optional): path to location of command binary file.
                Defaults to "".
        """
        super().__init__("/run/self_test/*", "self_test", path)

        self.group_name = FormattedParameter("--group-name {}")
        self.endpoint = FormattedParameter("--endpoint {0}")
        self.message_sizes = FormattedParameter("--message-sizes {0}")
        self.max_inflight_rpcs = FormattedParameter("--max-inflight-rpcs {0}")
        self.repetitions = FormattedParameter("--repetitions {0}")
        self.attach_info = FormattedParameter("--path {0}")
