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
from command_utils import BasicParameter, FormattedParameter
from avocado.utils import process

class IpmctlCommand(CommandWithParameters):
    """Defines a object representing a ipmctl command."""

    def __init__(self):
        """Create a ipmctl Command object."""
        super(IpmctlCommand, self).__init__("ipmctl")

        self.request = BasicParameter("{}")
        self.dimm = FormattedParameter("-dimm", False)

    def get_param_names(self):
        """Get a sorted list of dmg command parameter names."""
        names = self.get_attribute_names(FormattedParameter)
        names.insert(0, "request")
        return names

    def run(self, timeout=None, verbose=True, env=None):
        """ Run the ipmctl command.

        Args:
            timeout (int, optional): timeout in seconds. Defaults to None.
            verbose (bool, optional): display command output. Defaults to True.
            env (dict, optional): env for the command. Defaults to None.
            sudo (bool, optional): sudo will be prepended to the command.
                Defaults to True, ipmctl need to be run as root.

        Raises:
            process.CmdError: Avocado command exception

        Returns:
            process.CmdResult: CmdResult object containing the results from
            the command.

        """
        return process.run(self.__str__(), timeout, verbose, env=env,
                           shell=True, sudo=True)
