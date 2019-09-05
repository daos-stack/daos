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


class DmgCommand(DaosCommand):
    """Defines a object representing a dmg (or daos_shell) command."""

    def __init__(self, path):
        """Create a dmg Command object."""
        super(DmgCommand, self).__init__(
            "daos_shell", "/run/dmg/*", path)

        # daos_shell options
        self.hostlist = FormattedParameter("-l {}")
        self.hostfile = FormattedParameter("-f {}")
        self.configpath = FormattedParameter("-o {}")
        self.insecure = FormattedParameter("-i", None)

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

    def get_params(self, test):
        """Get values for all of the dmg command params using a yaml file.

        Sets each BasicParameter object's value to the yaml key that matches
        the assigned name of the BasicParameter object in this class. For
        example, the self.block_size.value will be set to the value in the yaml
        file with the key 'block_size'.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/dmg/*".

        """
        super(DmgCommand, self).get_params(test, path)
