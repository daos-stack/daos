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

class DmgCommand(CommandWithParameters):
    """Defines a object representing a dmg (or daos_shell) command."""

    def __init__(self, command="dmg"):
        """Create a dmg Command object
        Args:
            command (str): command to execute, default=dmg
        """
        super(DmgCommand, self).__init__(command, shell=True)

        self.request = BasicParameter("{}")
        self.action = BasicParameter("{}")

        # daos_shell options
        self.hostlist = FormattedParameter("-l {}")
        self.hostfile = FormattedParameter("-f {}")
        self.configpath = FormattedParameter("-o {}")
        self.port = BasicParameter("{}")

        # dmg options
        self.gid = FormattedParameter("--gid={}")
        self.uid = FormattedParameter("--uid={}")
        self.group = FormattedParameter("--group={}")
        self.mode = FormattedParameter("--mode={}")
        self.size = FormattedParameter("--size={}")
        self.nvme = FormattedParameter("--nvme={}")
        self.svcn = FormattedParameter("--svcn={}")
        self.target = FormattedParameter("--target={}")
        self.force = FormattedParameter("--force", False)
        self.pool = FormattedParameter("--pool={}")
        self.svc = FormattedParameter("--svc={}")
        self.rank = FormattedParameter("--rank={}")
        self.cont = FormattedParameter("--cont={}")
        self.oid = FormattedParameter("--oid={}")

    def __str__(self):
        """Return the command with all of its defined parameters as a string.
        The dmg command (or daos_shell) use the following command structure:
        dmg(daos_shell) <request> <action> <parameters>
        Returns:
            str: the command with all the defined parameters.
        """
        params = []
        for name in self.get_param_names():
            value = str(getattr(self, name))
            if (value != "" and name != "request" and name != "action" and
                    name != "port"):
                params.append(value)
        return " ".join([self._command] + params +
                        [str(getattr(self, "request"))] +
                        [str(getattr(self, "action"))])
