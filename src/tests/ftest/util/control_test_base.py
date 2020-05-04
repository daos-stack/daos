#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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

from avocado import fail_on
from apricot import TestWithServers
from general_utils
from command_utils import CommandFailure


class ControlTestBase(TestWithServers):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Defines common methods for control tests.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ControlTestBase object."""
        super(ControlTestBase, self).__init__(*args, **kwargs)
        self.dmg = None

    def setUp(self):
        """Set up each test case."""
        super(ControlTestBase, self).setUp()
        self.dmg = self.get_dmg_command()

    @fail_on(CommandFailure)
    def get_dmg_output(self, method_name, **kwargs):
        """Run the dmg command."""
        return self.dmg.get_output(method_name, **kwargs)

    def get_superblock_info(self, scm_mount, sp_value):
        """Get the superblock information for each host.

        Args:
            scm_mount (str): scm mount path.
            sp_value (str): superblock file value to extract.
                i.e. version, uuid, system, rank

        Returns:
            dict: a dictionary of data values for each NodeSet key

        """
        cmd = r"cat {} | sed -En 's/{}:\s+([a-z0-9-_]+).*/\1/p'".format(
            scm_mount, sp_value)
        text = "superblock"
        error = "Error obtaining superblock info: {}".format(sp_value)
        return get_host_data(self.dmg.hostlist, cmd, text, error, 20)


