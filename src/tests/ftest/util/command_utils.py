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
            return self._str_format.format(self.value)
        else:
            return ""


class ObjectWithParameters(object):
    """A class for an object with parameters."""

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

    def get_params(self, test, path):
        """Get values for all of the command params from the yaml file.
        Sets each BasicParameter object's value to the yaml key that matches
        the assigned name of the BasicParameter object in this class. For
        example, the self.block_size.value will be set to the value in the yaml
        file with the key 'block_size'.
        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.
        Args:
            test (Test): avocado Test object
            path (str): yaml namespace.
        """
        for name in self.get_param_names():
            getattr(self, name).get_yaml_value(name, test, path)


class CommandWithParameters(ObjectWithParameters):
    """A class for command with paramaters."""

    def __init__(self, command, path=""):
        """Create a CommandWithParameters object.
        Uses Avocado's utils.process module to run a command str provided.
        Args:
            path (str): path to location of command binary file
            command (str): string of the command to be executed.
        """
        self._command = command
        self._path = path

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
