#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import os
import yaml


class CommandFailure(Exception):
    """Base exception for this module."""


class BasicParameter():
    """A class for parameters whose values are read from a yaml file."""

    def __init__(self, value, default=None, yaml_key=None):
        """Create a BasicParameter object.

        Normal use includes assigning this object to an attribute name that
        matches the test yaml file key used to assign its value.  If the
        variable name will conflict with another class attribute, e.g. self.log,
        then the `yaml_key` argument can be used to define the test yaml file
        key independently of the attribute name.

        Args:
            value (object): initial value for the parameter
            default (object, optional): default value. Defaults to None.
            yaml_key (str, optional): the yaml key name to use when finding the
                value to assign from the test yaml file. Default is None which
                will use the object's variable name as the yaml key.
        """
        self._value = value if value is not None else default
        self._default = default
        self._yaml_key = yaml_key
        self.log = getLogger(__name__)

        # Flag used to indicate if a parameter value has or has not been updated
        self.updated = True

    def __str__(self):
        """Convert this BasicParameter into a string.

        Returns:
            str: the string version of the parameter's value

        """
        return str(self.value) if self.value is not None else ""

    @property
    def value(self):
        """Get the value of this setting.

        Returns:
            object: value currently assigned to the setting

        """
        return self._value

    @value.setter
    def value(self, item):
        """Set the value of this setting.

        Args:
            item (object): value to assign for the setting
        """
        if item != self._value:
            self._value = item
            self.updated = True

    def get_yaml_value(self, name, test, path):
        """Get the value for the parameter from the test case's yaml file.

        Args:
            name (str): name of the value in the yaml file
            test (Test): avocado Test object to use to read the yaml file
            path (str): yaml path where the name is to be found
        """
        if self._yaml_key is not None:
            # Use the yaml key name instead of the variable name
            name = self._yaml_key
        if hasattr(test, "config") and test.config is not None:
            self.value = test.config.get(name, path, self._default)
        else:
            self.value = test.params.get(name, path, self._default)

    def update(self, value, name=None, append=False):
        """Update the value of the parameter.

        Args:
            value (object): value to assign
            name (str, optional): name of the parameter which, if provided, is
                used to display the update. Defaults to None.
            append (bool, optional): append/extend/update the current list/dict
                with the provided value.  Defaults to False - override the
                current value.
        """
        if append and isinstance(self.value, list):
            if isinstance(value, list):
                # Add the new list of value to the existing list
                self.value.extend(value)
            else:
                # Add the new value to the existing list
                self.value.append(value)
            self.updated = True
        elif append and isinstance(self.value, dict):
            # Update the dictionary with the new key/value pairs
            self.value.update(value)
            self.updated = True
        else:
            # Override the current value with the new value
            self.value = value
        if name is not None:
            self.log.debug("Updated param %s => %s", name, self.value)

    def update_default(self, value):
        """Update the BasicParameter default value.

        Args:
            value (object): new default value
        """
        self._default = value


class FormattedParameter(BasicParameter):
    # pylint: disable=too-few-public-methods
    """A class for test parameters whose values are read from a yaml file."""

    def __init__(self, str_format, default=None, yaml_key=None):
        """Create a FormattedParameter  object.

        Normal use includes assigning this object to an attribute name that
        matches the test yaml file key used to assign its value.  If the
        variable name will conflict with another class attribute, e.g. self.log,
        then the `yaml_key` argument can be used to define the test yaml file
        key independently of the attribute name.

        Args:
            str_format (str): format string used to convert the value into an
                command line argument string
            default (object): default value for the param
            yaml_key (str, optional): alternative yaml key name to use when
                assigning the value from a yaml file. Default is None which
                will use the object's variable name as the yaml key.
        """
        super().__init__(default, default)
        self._str_format = str_format
        self._yaml_key = yaml_key

    def __str__(self):
        """Return a FormattedParameter object as a string.

        Returns:
            str: if defined, the parameter, otherwise an empty string

        """
        parameter = ""
        if isinstance(self._default, bool) and self.value:
            parameter = self._str_format
        elif not isinstance(self._default, bool) and self.value is not None:
            if isinstance(self.value, dict):
                parameter = " ".join([
                    self._str_format.format(
                        "{} \"{}\"".format(key, self.value[key]))
                    for key in self.value])
            elif isinstance(self.value, (list, tuple)):
                parameter = " ".join(
                    [self._str_format.format(value) for value in self.value])
            else:
                parameter = self._str_format.format(self.value)

        return parameter

    def get_yaml_value(self, name, test, path):
        """Get the value for the parameter from the test case's yaml file.

        Args:
            name (str): name of the value in the yaml file - not used
            test (Test): avocado Test object to use to read the yaml file
            path (str): yaml path where the name is to be found
        """
        if self._yaml_key is not None:
            # Use the yaml key name instead of the variable name
            name = self._yaml_key
        return super().get_yaml_value(name, test, path)


class LogParameter(FormattedParameter):
    """A class for a test log file parameter which is read from a yaml file."""

    def __init__(self, directory, str_format, default=None):
        """Create a LogParameter  object.

        Args:
            directory (str): fixed location for the log file name specified by
                the yaml file
            str_format (str): format string used to convert the value into an
                command line argument string
            default (object): default value for the param
        """
        super().__init__(str_format, default)
        self._directory = directory
        self._add_directory()

    def _add_directory(self):
        """Add the directory to the log file name assignment.

        The initial value is restricted to just the log file name as the
        location (directory) of the file is fixed.  This method updates the
        initial log file value (just the log file name) to include the directory
        and name for the log file.
        """
        if isinstance(self.value, str):
            name = os.path.basename(self.value)
            self.value = os.path.join(self._directory, name)
        elif self.value is not None:
            self.log.info(
                "Warning: '%s' not added to '%s' due to incompatible type: %s",
                self._directory, self.value, type(self.value))

    def get_yaml_value(self, name, test, path):
        """Get the value for the parameter from the test case's yaml file.

        Args:
            name (str): name of the value in the yaml file
            test (Test): avocado Test object to use to read the yaml file
            path (str): yaml path where the name is to be found
        """
        super().get_yaml_value(name, test, path)
        self._add_directory()
        self.log.debug("  Added the directory: %s => %s", name, self.value)

    def update(self, value, name=None, append=False):
        """Update the value of the parameter.

        Args:
            value (object): value to assign
            name (str, optional): name of the parameter which, if provided, is
                used to display the update. Defaults to None.
            append (bool, optional): append/extend/update the current list/dict
                with the provided value.  Defaults to False - override the
                current value.
        """
        super().update(value, name, append)
        self._add_directory()
        self.log.debug("  Added the directory: %s => %s", name, self.value)


class ObjectWithParameters():
    """A class for an object with parameters."""

    def __init__(self, namespace):
        """Create a ObjectWithParameters object.

        Args:
            namespace (str): yaml namespace (path to parameters)
        """
        self.namespace = namespace
        self.log = getLogger(__name__)

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

        Note: Override this method to change the order or inclusion of a
            command parameter in the get_params() method.

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
            getattr(self, name).get_yaml_value(name, test, self.namespace)


class CommandWithParameters(ObjectWithParameters):
    """A class for command with parameters."""

    def __init__(self, namespace, command, path=""):
        """Create a CommandWithParameters object.

        Uses Avocado's utils.process module to run a command str provided.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file.
                Defaults to "".
        """
        super().__init__(namespace)
        self._command = command
        self._path = path
        self._pre_command = None

    @property
    def command(self):
        """Get the command without its parameters."""
        return self._command

    @property
    def command_path(self):
        """Get the path used for the command."""
        return self._path

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        # Join all the parameters that have been assigned a value with the
        # command to create the command string
        params = []
        for name in self.get_str_param_names():
            value = str(getattr(self, name))
            if value != "":
                params.append(value)

        # Append the path to the command and prepend it with any other
        # specified commands
        command_list = [] if self._pre_command is None else [self._pre_command]
        command_list.append(os.path.join(self._path, self._command))

        # Return the command and its parameters
        return " ".join(command_list + params)

    def get_str_param_names(self):
        """Get a sorted list of the names of the command attributes.

        Returns:
            list: a list of class attribute names used to define parameters
                for the command.

        """
        return self.get_param_names()


class YamlParameters(ObjectWithParameters):
    """A class of parameters used to create a yaml file."""

    def __init__(self, namespace, filename=None, title=None, other_params=None):
        """Create a YamlParameters object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            filename (str): the yaml file to generate with the parameters
            title (str, optional): namespace under which to place the
                parameters when creating the yaml file. Defaults to None.
            other_params (YamlParameters, optional): yaml parameters to
                include with these yaml parameters. Defaults to None.
        """
        super().__init__(namespace)
        self.filename = filename
        self.title = title
        self.other_params = other_params

    def get_params(self, test):
        """Get values for the yaml parameters from the test yaml file.

        Args:
            test (Test): avocado Test object
        """
        # Get the values for the yaml parameters defined by this class
        super().get_params(test)

        # Get the values for the yaml parameters defined by the other class
        if self.other_params is not None:
            self.other_params.get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        if (self.other_params is not None and
                hasattr(self.other_params, "get_yaml_data")):
            yaml_data = self.other_params.get_yaml_data()
        else:
            yaml_data = {}
        for name in self.get_param_names():
            value = getattr(self, name).value
            if value is not None and value is not False:
                yaml_data[name] = value

        return yaml_data if self.title is None else {self.title: yaml_data}

    def is_yaml_data_updated(self):
        """Determine if any of the yaml file parameters have been updated.

        Returns:
            bool: whether or not a yaml file parameter has been updated

        """
        yaml_data_updated = False
        if (self.other_params is not None and
                hasattr(self.other_params, "is_yaml_data_updated")):
            yaml_data_updated = self.other_params.is_yaml_data_updated()
        if not yaml_data_updated:
            for name in self.get_param_names():
                if getattr(self, name).updated:
                    yaml_data_updated = True
                    break
        return yaml_data_updated

    def reset_yaml_data_updated(self):
        """Reset each yaml file parameter updated state to False."""
        if (self.other_params is not None and
                hasattr(self.other_params, "reset_yaml_data_updated")):
            self.other_params.reset_yaml_data_updated()
        for name in self.get_param_names():
            getattr(self, name).updated = False

    def create_yaml(self, filename=None):
        """Create a yaml file from the parameter values.

        A yaml file will only be created if at least one of its parameter values
        have be updated (BasicParameter.updated = True).

        Args:
            filename (str, optional): the yaml file to generate with the
                parameters. Defaults to None, which uses self.filename.

        Raises:
            CommandFailure: if there is an error creating the yaml file

        Returns:
            bool: whether or not an updated yaml file was created

        """
        create_yaml = self.is_yaml_data_updated()
        if create_yaml:
            # Write a new yaml file if any of the parameters have been updated
            if filename is None:
                filename = self.filename
            yaml_data = self.get_yaml_data()
            self.log.info("Writing yaml configuration file %s", filename)
            try:
                with open(filename, 'w') as write_file:
                    yaml.dump(yaml_data, write_file, default_flow_style=False)
            except Exception as error:
                raise CommandFailure(
                    "Error writing the yaml file {}: {}".format(
                        filename, error)) from error
            self.reset_yaml_data_updated()
        return create_yaml

    def set_value(self, name, value):
        """Set the value for a specified attribute name.

        Args:
            name (str): name of the attribute for which to set the value
            value (object): the value to set

        Returns:
            bool: if the attribute name was found and the value was set

        """
        status = False
        setting = getattr(self, name, None)
        if setting is not None and hasattr(setting, "update"):
            setting.update(value, name)
            status = True
        elif setting is not None:
            setattr(self, name, value)
            self.log.debug("Updated param %s => %s", name, value)
            status = True
        elif self.other_params is not None:
            status = self.other_params.set_value(name, value)
        return status

    def get_value(self, name):
        """Get the value of the specified attribute name.

        Args:
            name (str): name of the attribute from which to get the value

        Returns:
            object: the object's value referenced by the attribute name

        """
        setting = getattr(self, name, None)
        if setting is not None and hasattr(setting, "value"):
            value = setting.value
        elif setting is not None:
            value = setting
        elif self.other_params is not None:
            value = self.other_params.get_value(name)
        else:
            value = None
        return value


class TransportCredentials(YamlParameters):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self, namespace, title, log_dir):
        """Initialize a TransportConfig object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            title (str, optional): namespace under which to place the
                parameters when creating the yaml file. Defaults to None.
        """
        super().__init__(namespace, None, title)
        default_insecure = str(os.environ.get("DAOS_INSECURE_MODE", True))
        default_insecure = default_insecure.lower() == "true"
        self.ca_cert = LogParameter(log_dir, None, "daosCA.crt")
        self.allow_insecure = BasicParameter(None, default_insecure)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        yaml_data = super().get_yaml_data()

        # Convert the boolean value into a string
        if self.title is not None:
            yaml_data[self.title]["allow_insecure"] = self.allow_insecure.value
        else:
            yaml_data["allow_insecure"] = self.allow_insecure.value

        return yaml_data

    def get_certificate_data(self, name_list):
        """Get certificate data by name_list.

        Args:
            name_list (list): list of certificate attribute names.

        Returns:
            data (dict): a dictionary of parameter directory name keys and
                value.

        """
        data = {}
        if not self.allow_insecure.value:
            for name in name_list:
                value = getattr(self, name).value
                if isinstance(value, str):
                    dir_name, file_name = os.path.split(value)
                    if dir_name not in data:
                        data[dir_name] = [file_name]
                    else:
                        data[dir_name].append(file_name)
        return data


class CommonConfig(YamlParameters):
    """Defines common daos_agent and daos_server configuration file parameters.

    Includes:
        - the daos system name (name)
        - a list of access point nodes (access_points)
        - the default port number (port)
        - the transport credentials
    """

    def __init__(self, name, transport):
        """Initialize a CommonConfig object.

        Args:
            name (str): default value for the name configuration parameter
            transport (TransportCredentials): transport credentails
        """
        super().__init__(
            "/run/common_config/*", None, None, transport)

        # Common configuration parameters
        #   - name: <str>, e.g. "daos_server"
        #       Name associated with the DAOS system.
        #
        #   - access_points: <list>, e.g.  ["hostname1:10001"]
        #       Hosts can be specified with or without port, default port below
        #       assumed if not specified. Defaults to the hostname of this node
        #       at port 10000 for local testing
        #
        #   - port: <int>, e.g. 10001
        #       Default port number with with to bind the daos_server. This
        #       will also be used when connecting to access points if the list
        #       only contains host names.
        #
        self.name = BasicParameter(None, name)
        self.access_points = BasicParameter(None, ["localhost"])
        self.port = BasicParameter(None, 10001)


class EnvironmentVariables(dict):
    """Dictionary of environment variable keys and values."""

    def copy(self):
        """Return a copy of this object.

        Returns:
            EnvironmentVariables: a copy of this object

        """
        return EnvironmentVariables(self)

    def get_list(self):
        """Get a list of environment variable assignments.

        Returns:
            list: a list of environment variable assignment (key=value) strings

        """
        return [
            key if value is None else "{}={}".format(key, value)
            for key, value in list(self.items())
        ]

    def get_export_str(self, separator=";"):
        """Get the command to export all of the environment variables.

        Args:
            separator (str, optional): export command separator.
                Defaults to ";".

        Returns:
            str: a string of export commands for each environment variable

        """
        export_list = ["export {}".format(export) for export in self.get_list()]
        export_str = separator.join(export_list)
        if export_str:
            export_str = "".join([export_str, separator])
        return export_str

class PositionalParameter(BasicParameter):
    """Parameter that defines position.

    Used to support positional parameters for dmg and daos.
    """

    def __init__(self, position, default=None):
        """Create a PositionalParameter  object.

        Args:
            position (int): argument position/order
            default (object, optional): default value for the param. Defaults to
                None.

        """
        super().__init__(default, default)
        self._position = position

    @property
    def position(self):
        """Position property that defines the position of the parameter.

        """
        return self._position

    def __lt__(self, other):
        return self.position < other.position

    def __gt__(self, other):
        return self.position > other.position

    def __eq__(self, other):
        return self.position == other.position

    def __hash__(self):
        """Returns self.position as the hash of the class.

        This is used in CommandWithPositionalParameters.get_attribute_names()
        where we use this object as the key for a dictionary.

        """
        return self.position

class CommandWithPositionalParameters(CommandWithParameters):
    """Command that uses positional parameters.

    Used to support positional parameters for dmg and daos.
    """

    def get_attribute_names(self, attr_type=None):
        """Get a sorted list of the names of the attr_type attributes.

        The list has the ordered positional parameters first, then
        non-positional parameters.

        Args:
            attr_type(object, optional): A single object type or tuple of
                object types used to filter class attributes by their type.
                Defaults to None.

        Returns:
            list: a list of class attribute names used to define parameters

        """
        positional = {}
        non_positional = []
        for name in sorted(list(self.__dict__)):
            attr = getattr(self, name)
            if isinstance(attr, attr_type):
                if hasattr(attr, "position"):
                    positional[attr] = name
                else:
                    non_positional.append(name)
        return [positional[key] for key in sorted(positional)] + non_positional
