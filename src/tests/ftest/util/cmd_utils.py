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

from avocado.utils import process

class CommandLineFailure(Exception):
    """Raise if Command failed."""
    pass

class Command(object):
    """Command Defines an object represeting a command line string."""

    def __init__(self, tool, path="", params=None):
        """Create a Command object

        Args:
            tool (str): string of the tool executable
            params (list): list of object parameters, Defaults to None

        """
        self.tool = tool
        self.path = path
        if params is None:
            self.params = {}
        self.params = params

    def __str__(self):
        """Return a Command object as a string

        Returns:
            str: the command string

        """
        options = []

        # Get command and action to perform for tool
        command = str(self.params["command"].value)
        action = str(self.params["action"].value)

        if command == "None":
            command = ""
        if action == "None":
            action = ""

        for param_name, param_obj in self.params.items():
            if isinstance(param_obj, CommandParam):
                if param_name not in ["command", "action"]:
                    value_str = param_obj.__str__()
                    if (str(param_obj.value) != "None" and
                        str(param_obj.value) != ""):
                        options.append(value_str)
        return self.tool + " " + " ".join(options) + " "+ command + " " + action

    def set_param_values(self, test):
        """Set values for all of the command params using a yaml file.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/ior/*".

        """
        for param_name, param_obj in self.params.items():
            if isinstance(param_obj, CommandParam):
                param_obj.set_yaml_value(param_name, test, self.path)

    def run(self, command, bg = False):
        """ Run the command provided and handle command failure

        Args:
            command (str): full command string
            bg (bool): if true, run command in the backgroung. Default False

        """

        cmd_obj = None

        try:
            if bg == False:
                cmd_obj = process.run(command, verbose = True, sudo = True,
                    shell = True)
            else:
                cmd_obj = process.SubProcess(
                    command, verbose = True, sudo = True,
                    ignore_bg_processes = True)
        except Exception as excpn:
            raise CommandLineFailure("<Command Error>:{}".format(command))

        return cmd_obj

class CommandParam(object):
    """Defines an object represeting a single command line parameter."""

    def __init__(self, str_format, default=None):
        """Create a CommandParam object.

        Args:
            str_format (str): format string used to convert the value into an
                command line argument string
            default (object): default value for the param

        """
        self.str_format = str_format
        self.default = default
        self.value = default

    def __str__(self):
        """Return a CommandParam object as a string.

        Returns:
            str: if defined, the cmd parameter, otherwise an empty string

        """
        if self.default == None and self.value:
            return self.str_format.format(self.value)
        elif self.default and self.value:
            return self.str_format.format(self.value)
        else:
            return ""

    def set_yaml_value(self, name, test, path=""):
        """Set the value of the parameter using the test's yaml file value.

        Args:
            name (str): name associated with the value in the yaml file
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "".

        """
        self.value = test.params.get(name, path)