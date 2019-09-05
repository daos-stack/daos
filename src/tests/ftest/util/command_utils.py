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

import os

from avocado.utils import process, CmdError

class CommandFailure(Exception):
    """Handle when command run has failed or timed out."""

class BasicParameter(object):
    """A class for parameters whose values are read from a yaml file."""

    def __init__(self, value, default=None):
        """Create a BasicParameter object.

        Args:
            value (object): intial value for the parameter
            default (object, optional): default value. Defaults to None.
        """
        self.value = value if value is not None else default
        self._default = default

    def __str__(self):
        """Convert this BasicParameter into a string.

        Returns:
            str: the string version of the parameter's value

        """
        return str(self.value) if self.value is not None else ""

    def get_yaml_value(self, name, test, path):
        """Get the value for the parameter from the test case's yaml file.

        Args:
            name (str): name of the value in the yaml file
            test (Test): avocado Test object to use to read the yaml file
            path (str): yaml path where the name is to be found
        """
        self.value = test.params.get(name, path, self._default)

    def update(self, value, name=None):
        """Update the value of the parameter.

        Args:
            value (object): value to assign
            name (str, optional): name of the parameter which, if provided, is
                used to display the update. Defaults to None.
        """
        self.value = value
        if name is not None:
            print("Updated param {} => {}".format(name, self.value))


class FormattedParameter(BasicParameter):
    # pylint: disable=too-few-public-methods
    """A class for test parameters whose values are read from a yaml file."""

    def __init__(self, str_format, default=None):
        """Create a FormattedParameter  object.

        Args:
            str_format (str): format string used to convert the value into an
                command line argument string
            default (object): default value for the param
        """
        super(FormattedParameter, self).__init__(default, default)
        self._str_format = str_format

    def __str__(self):
        """Return a FormattedParameter object as a string.

        Returns:
            str: if defined, the parameter, otherwise an empty string

        """
        if isinstance(self._default, bool) and self.value:
            return self._str_format
        elif not isinstance(self._default, bool) and self.value is not None:
            if isinstance(self.value, (list, tuple)):
                return ",".join(
                    [self._str_format.format(value) for value in self.value])
            else:
                return self._str_format.format(self.value)
        else:
            return ""


class EnvironmentParameter(FormattedParameter):
    """A class for the environment parameters."""

    def __str__(self):
        """Return a EnvironmentParameter object as a string.

        Returns:
            str: if defined, the parameter, otherwise an empty string

        """
    def format_env_param(self):
        """format_env_param [summary]

        [extended_summary]
        """

class ObjectWithParameters(object):
    """A class for an object with parameters."""

    def __init__(self, namespace):
        """Create object with parameter objects.

        Args:
            namespace (str, optional): path to location of test yaml file.
        """
        self.namespace = namespace

    def get_attribute_names(self, attr_type=None):
        """Get a sorted list of the names of the attr_type attributes.

        Args:
            attr_type(object, optional): A single object type or tuple of
                object types used to filter class attributes by their type.
                Defaults to None.

        Returns:
            list: a list of class attribute names used to define parameters

        """
        return [
            name for name in sorted(self.__dict__.keys())
            if attr_type is None or isinstance(getattr(self, name), attr_type)]

    def get_param_names(self):
        """Get a sorted list of the names of the BasicParameter attributes.

        Returns:
            list: a list of class attribute names used to define parameters

        """
        return self.get_attribute_names(BasicParameter)

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Sets each BasicParameter object's value to the yaml key that matches
        the assigned name of the BasicParameter object in this class. For
        example, the self.block_size.value will be set to the value in the yaml
        file with the key 'block_size'.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        for name in self.get_param_names():
            getattr(self, name).get_yaml_value(name, test, self._namespace)


class CommandWithParameters(ObjectWithParameters):
    """A class for command with paramaters."""

    def __init__(self, command, namespace, path=""):
        """Create a CommandWithParameters object.

        Uses Avocado's utils.process module to run a command str provided.

        Args:
            command (str): string of the command to be executed.
            namespace (str): path to location of test yaml file.
            path (str): path to location of command binary.
        """
        super(CommandWithParameters, self).__init__(namespace)
        self._command = command
        self._path = path
        self.timeout=None
        self.verbose=True
        self.env=None
        self.sudo=False
        self.kwargs = {
            "cmd": self.__str__(),
            "timeout": self.timeout,
            "verbose": self.verbose,
            "allow_output_check": "combined",
            "shell": True,
            "env": self.env,
            "sudo": self.sudo,
        }

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        # Join all the parameters that have been assigned a value with the
        # command to create the command string
        params = []
        for name in self.get_param_names():
            value = str(getattr(self, name))
            if value != "":
                params.append(value)
        return " ".join([os.path.join(self._path, self._command)] + params)

    def run(self):
        """ Run the command.

        This run will stop the test flow until the command has completed.

        Raises:
            process.CmdError: Avocado command exception

        Returns:
            process.CmdResult: CmdResult object containing the results from
            the command.

        """
        try:
            process.run(**self.kwargs)

        except process.CmdError as error:
            print("<{}> Exception occurred: {}".format(self._command, error))
            raise CommandFailure("Run process Failed: {}".format(error))


class DaosCommand(CommandWithParameters):
    """A class for similar daos command line tools."""

    def __init__(self, command, path=""):
        """Create DaosCommand object.

        Specific type of command object built so command str returns:
            <command> <options> <request> <action>

        Args:
            command (str): string of the command to be executed.
            path (str): path to location of daos command binary.
        """
        super(DaosCommand, self).__init__(command, path)
        self.request = BasicParameter("{}")
        self.action = BasicParameter("{}")

    def get_param_names(self):
        """Get a sorted list of DaosCommand parameter names."""
        names = self.get_attribute_names(FormattedParameter)
        names.extend(["request", "action"])
        return names


class JobManagerCommand(CommandWithParameters):
    """A class for job manager commands."""

    def __init__(self, managercmd, namespace):
        """Create a JobManager object.

        Construct and run a job manager tool. e.g. mpirun, orterun, slurm

        Args:
            managercmd (str): string of the manager tool to be executed
            namespace (str): path to location of test yaml file.
        """
        super(JobManagerCommand, self).__init__(managercmd, namespace)
        self.command_to_manage = None
        self.process = None
        self.process_indicator = None
        self.hosts = []

    def get_param_names(self):
        """Get a sorted list of JobManagerCommand parameter names."""
        names = self.get_attribute_names(EnvironmentParameter)
        names.insert(0, "export")
        return names

    def run(self):
        """ Run the command as a subprocess."""


        self.process = process.SubProcess(**self.kwargs)

class OrterunCommand(JobManagerCommand):
    """ A class to handle  orterun command."""

    def __init__(self, path):
        """Create a orterun command object."""
        super(OrterunCommand, self).__init__(
            "orterun", "/run/orterun/*", path)

        self.hosts = FormattedParameter("--host", None)
        self.hostfile = FormattedParameter("--hostfile", None)
        self.processes = FormattedParameter("-np", 1)
        self.display_map = FormattedParameter("--display-map", True)
        self.map_by = FormattedParameter("--map-by", "node")
        self.export = FormattedParameter("-x", None)
        self.enable_recovery = FormattedParameter("--enable-recovery", True)
        self.report_uri = FormattedParameter("--report-uri", None)

    def get_host_count(self):
        if self.hosts.value us not None:
            return len(self.hosts.value)
