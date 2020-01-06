#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

from command_utils import CommandWithParameters
from command_utils import DaosCommand as CommandWithSubCommand

class DaosCommand(CommandWithSubCommand):
    """Defines a object representing a daos command."""

    def __init__(self, path):
        """Create a daos Command object."""
        super(DaosCommand, self).__init__("/run/daos/*", "daos", path)

    def get_action_command(self):
        """Assign a command object for the specified request and action."""
        # pylint: disable=redefined-variable-type
        if self.action.value == "create":
            self.action_command = self.DaosCreateSubCommand()
        else:
            self.action_command = None

    class DaosCreateSubCommand(CommandWithParameters):
        """Defines a object representing a create sub daos command."""

        def __init__(self):
            """Create a daos Command object."""
            super(DaosCommand.DaosCreateSubCommand, self).__init__(
                "/run/daos/create/*", "create")
