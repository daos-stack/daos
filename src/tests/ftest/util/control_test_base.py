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

from apricot import TestWithServers
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
        super(ControlTestBase, self).setUp()
        self.dmg = self.get_dmg_command()
        self.dmg.get_params(self)

    def get_dmg_output(self, method_name, **kwargs):
        """Run the dmg command."""
        try:
            info = self.dmg.get_output(method_name, **kwargs)
        except CommandFailure as err:
            self.fail("Failed running command: {}".format(err))
        return info
