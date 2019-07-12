#!/usr/bin/python
'''
    (C) Copyright 2019 Intel Corporation.

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
    '''
from __future__ import print_function

import os
import json
from cmd_utils import Command, CommandParam

class DmgCmdUtils(Command):
    """Defines a object representing a dmg (or daos_shell) command."""

    def __init__(self, tool = "dmg"):
        """Create a dmg Command object

        Args:
            tool (str): tool to execute, default = dmg

        """

        self.command = CommandParam("{}")           # i.e. storage, network
        self.action = CommandParam("{}")            # i.e. format, scan

        # daos_shell options
        self.hostlist = CommandParam("-l {}")       # list of addresses
        self.hostfile = CommandParam("-f {}")       # path of hostfile
        self.configpath = CommandParam("-o {}")     # client config file path

        # dmg options


        self.cmd = Command(tool, "/run/{}/*".format(tool), self.__dict__)

    def execute(self, test, basepath = "", bg = False):
        """Run the dmg command

        Args:
            test (object): Avocado test object
            basepath (str, optional): DAOS install dir. Defaults to "".

        Return:
            Avocado cmd object.

        """

        # Set param values and get command string
        self.cmd.set_param_values(test)
        command = os.path.join(basepath, 'install', 'bin', self.cmd.__str__())

        return self.cmd.run(command)