#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import re

from command_utils_base import BasicParameter, ObjectWithParameters
from general_utils import get_host_data


DATA_ERROR = "[ERROR]"


class ConfigurationData(object):
    """Defines requirement data for a set of hosts."""

    def __init__(self, hosts, timeout=120):
        """Initialize a ConfigurationData object.

        Args:
            hosts (list): list of hosts from which to obtain data
            timeout (int, optional): timeout used when obtaining host data.
                Defaults to 120 seconds.
        """
        self.log = getLogger(__name__)
        self.hosts = hosts
        self.timeout = timeout

        # Defines the methods used to obtain the host data for each
        # ConfigurationParameters BasicParameter attribute (requirement)
        self._data_key_map = {
            "mem_size": self.get_host_mem_data,
            "nvme_size": self.get_host_nvme_data,
            "scm_size": self.get_host_scm_data
        }

        # Stores the data from each host for each requirement name
        self._data = {}

    def get_host_mem_data(self):
        """Get the total non-swap memory in bytes for each host.

        Returns:
            dict: a dictionary of data values for each NodeSet key

        """
        cmd = r"free -b | sed -En 's/Mem:\s+([0-9]+).*/\1/p'"
        text = "memory"
        error = "Error obtaining total memory size"
        return get_host_data(self.hosts, cmd, text, error, self.timeout)

    def get_host_nvme_data(self):
        """Get the largest NVMe capacity in bytes for each host.

        Returns:
            dict: a dictionary of data values for each NodeSet key

        """
        cmd = "lsblk -b -o SIZE,NAME | grep nvme"
        text = "NVMe"
        error = "No NVMe drives bound to the kernel driver detected"
        return get_host_data(self.hosts, cmd, text, error, self.timeout)

    def get_host_scm_data(self):
        """Get the total SCM capacity in bytes for each host.

        Returns:
            dict: a dictionary of data values for each NodeSet key

        """
        cmd_list = [
            "sudo -n ipmctl show -units B -memoryresources",
            r"sed -En 's/^Capacity\=([0-9+]).*/\1/p'",
        ]
        cmd = " | ".join(cmd_list)
        text = "SCM"
        error = "No SCM devices detected"
        return get_host_data(self.hosts, cmd, text, error, self.timeout)

    def get_data(self, requirememt):
        """Get the specified requirement data.

        Args:
            requirememt (str): requirememt name

        Raises:
            AttributeError: if the requirement key is invalid

        Returns:
            dict: a dictionary of the requested data keyed by the NodeSet of
                hosts with the same data value

        """
        if requirememt in self._data:
            # Return the previously stored requested data
            return self._data[requirememt]
        elif requirememt in self._data_key_map:
            # Obtain, store (for future requests), and return the data
            self._data[requirememt] = self._data_key_map[requirememt]()
            return self._data[requirememt]
        else:
            # No known method for obtaining this data
            raise AttributeError(
                "Unknown data requirememt for ConfigurationData object: "
                "{}".format(requirememt))


class ConfigurationParameters(ObjectWithParameters):
    """Defines a configuration with a set of requirement parameters."""

    def __init__(self, namespace, name, data):
        """Create a ConfigurationParameters object.

        Args:
            name (str): configuration name; used to define param namespace
            data (ConfigurationData): object retaining the host data needed to
                verify the configuration requirement
        """
        super(ConfigurationParameters, self).__init__(namespace + name + "/*")
        self.name = name
        self._config_data = data

        # Define the yaml entries that define the configuration
        #  - Make sure to add any new parameter names defined here in the
        #    ConfigurationData._data_key_map dictionary
        self.mem_size = BasicParameter(0, 0)
        self.nvme_size = BasicParameter(0, 0)
        self.scm_size = BasicParameter(0, 0)

    def verify(self):
        """Determine if the list of hosts meet the configuration requirements.

        Raises:
            AttributeError: if one of the requirements is not defined with an
                integer value

        Returns:
            bool: True if all of the hosts meet the configuration requirements

        """
        # Verify each requirement against the list of hosts defined in the
        # ConfigurationData object
        for requirement in self.get_param_names():
            # Get the required minimum value for this requirement
            value = getattr(self, requirement).value
            try:
                value = int(value)
            except ValueError:
                raise AttributeError(
                    "Invalid '{}' attribute - must be an int: {}".format(
                        requirement, value))

            # Only verify requirements with a minimum
            if value != 0:
                # Retrieve the data for all of the hosts which is grouped by
                # hosts with the same values
                requirement_data = self._config_data.get_data(requirement)
                for group, data in requirement_data.items():
                    status = data != DATA_ERROR and data >= value
                    self.log.debug(
                        "  %s: Verifying the maximum %s meets the requirememt: "
                        "%s >= %s: %s",
                        group, requirement.replace("_", " "), data, value,
                        str(status))
                    if not status:
                        break

        return status


class Configuration(object):
    """Defines a means of obtaining configuration-specific test parameters."""

    def __init__(self, test_params, hosts, timeout=120, debug=True):
        """Initialize a RequirementsManager object.

        Args:
            test_params (AvocadoParams): avocado Test parameters
            timeout (int, optional): timeout in seconds for host data gathering.
                Defaults to 120 seconds.
            debug (bool, optional): display debug messages. Defaults to False.
        """
        self.log = getLogger(__name__)
        self.namespace = "/run/configurations/"
        self._data = ConfigurationData(hosts, timeout)
        self.debug = debug

        # All of the current test parameters as a dictionary of list of tuples
        # of parameter key and value indexed by the parameter path
        self._all_params = {}

        # All of the test parameters that are available for the active
        # configuration, stored as a dictionary of list of tuples of parameter
        # key and value indexed by the parameter path
        self._available_params = {}

        # All possible configuration names - defined by the test yaml parameters
        self._all_names = []

        # All configuration names that are valid for a specified list of hosts
        self.available_names = []

        # The current active configuration name
        self._active_name = None

        # A list of all of the configuration names that are not active -
        # includes both valid and invalid configuration names
        self._inactive_names = []

        # Populate the dictionary of lists of all paths and list of all
        # configuration names from the test parameter
        self._set_test_parameters(test_params)

    @property
    def active_name(self):
        """Get the active configuration name.

        Returns:
            str: the active configuration name

        """
        return self._active_name

    @active_name.setter
    def active_name(self, value):
        """Set the active configuration name.

        Args:
            value (str): a configuration name to set as active
        """
        if value in self.available_names:
            # Assign the valid active configuration name and update the list of
            # inactive configurations
            self._active_name = value
            self._log_debug(
                " - Active configuration name:       %s", self._inactive_names)
            self._inactive_names = [
                name for name in self._all_names if name != self.active_name]
            self._log_debug(
                " - Inactive configuration names:    %s", self._inactive_names)
        else:
            # Default to no active configuration
            self._active_name = None
            self._inactive_names = [name for name in self._all_names]

            # Report errors for invalid configutaion names
            if value is not None:
                self.log.error("Invalid configuration name: %s", value)

        # Update the paths through which parameter values can be obtained for
        # the new active configuration
        self._set_available_params()

    def _set_test_parameters(self, test_params):
        """Set the object parameters based upon the test parameter items.

        Args:
            test_params (AvocadoParams): avocado Test parameters
        """
        # Initialize the object attributes
        self._all_names = []
        self._all_params.clear()

        for path, key, value in test_params.iteritems():
            # Store all the non-configuration-definition parameters
            if path in self._all_params:
                self._all_params[path].append((key, value))
            else:
                self._all_params[path] = [(key, value)]

            # Add the configuration name for each configuration path
            if path.startswith(self.namespace):
                name = path[len(self.namespace):]
                if name not in self._all_names:
                    self._all_names.append(name)

    def _set_available_params(self):
        """Set the available parameters for the active configuration.

        Exclude any paths including non-active configurations.
        """
        self._available_params = {
            path: data
            for path, data in self._all_params.items()
            if self._valid_path(path)
        }

    def _valid_path(self, path):
        """Is the specified path valid for the active configuration.

        Note:
            Does not support paths with wildcards.
            Assumes configuration names are always at the end of the path.

        Args:
            path (str): a test parameter path (delimited by "/")

        Returns:
            bool: True if the path does not end in an inactive path

        """
        if path.startswith(self.namespace):
            # Always include configuration definition paths in order to be able
            # to find the requirements for each configuration
            return True
        else:
            # Otherwise only include paths that are not configuration-specific
            # or are specific to the active configuration
            return path.split("/")[-1] not in self._inactive_names

    def _set_available_names(self, test):
        """Set the list of available configuration names.

        Configuration names are listed as available if all of the specified
        hosts meet the configuration's requirements.

        Args:
            test (Test): avocado Test object
        """
        self.available_names = []
        for name in self._all_names:
            # Get each configuration's requirements and determine its viablity
            self.log.info("Verifying the %s configuration", name)
            config_params = ConfigurationParameters(
                self.namespace, name, self._data)
            config_params.get_params(test)
            if config_params.verify():
                # Add valid configurations to the available config list
                self.available_names.append(name)

    def _log_debug(self, msg, *args, **kwargs):
        """Log a debug message if enabled.

        Args:
            message (str): debug message to log
        """
        if self.debug:
            self.log.debug(msg, *args, **kwargs)

    def set_config(self, test):
        """Set the configuration whose requirements pass on the hosts.

        If multiple configuration's requirements pass on the specified list of
        hosts, select the first item in a sorted list.

        Args:
            test (Test): avocado Test object

        Returns:
            bool: True if at least one configuration passes its requirements on
                all of the specified hosts

        """
        # Optionally display all the paths of which the test is aware
        self._log_debug(
            "ALL_PARAMS:\n - %s",
            "\n - ".join(
                ["{}: {}".format(path, data)
                 for path, data in self._all_params.items()])
        )

        # Reset the available, active, and inactive configuration names
        self.active_name = None
        self._set_available_names(test)

        # Select the first available (sorted by name) configuration
        status = True
        if self.available_names:
            self.active_name = sorted(self.available_names)[0]
            self.log.info("Test configuration: %s", self.active_name)
        elif self._all_names:
            self.log.error("Test requirements not met!")
            status = False

        self._log_debug(
            "AVAILABLE_PARAMS:\n - %s",
            "\n - ".join(
                ["{}: {}".format(key, paths)
                 for key, paths in self._available_params.items()])
        )

        return status

    def get(self, key, path=None, default=None):
        """Get the value associated with key from the parameters.

        Args:
            key (str): the yaml key to search for
            path (str, optional): namespace to search for the key.
                Defaults to None - any relative path.
            default (object, optional): value to assign if key is not found.
                Defaults to None.
        """
        # Convert a glob-style path into regular expression
        if path is None:
            # Match any path
            re_path = ""
        elif path.endswith("*"):
            # Remove the trailing "*"
            re_path = "/".join(path.split("/")[:-1])
        else:
            # Without a trailing "*", only match the path specifified
            re_path = "".join([path, "$"])
        # Replace any "*" not at the end of the path
        search_path = re.compile(re_path.replace('*', '[^/]*'))

        # Debug
        self._log_debug(
            "Obtaining value for '%s' in '%s' (%s) for the '%s' configuration:",
            key, path, search_path.pattern, self.active_name)

        # Find each available path that matches the specified path and includes
        # the requested key
        matches = {}
        for apath, adata in self._available_params.items():
            if search_path.search(apath) is not None:
                self._log_debug(
                    " - Available path match:                  %s", apath)
                for akey, avalue in adata:
                    if akey == key:
                        self._log_debug(
                            "   - Available path key match:            %s: %s",
                            akey, avalue)
                        matches[apath] = avalue
        self._log_debug(
            " - Available paths containing this key:   %s", matches)

        # Determine which value to return for the key in the path
        multiple_matches = False
        if len(matches) == 0:
            # No matching paths for the key - use the default
            value = default
        elif len(matches) == 1:
            # A single matching path for the key - use the path's value
            path = list(matches.keys())[0]
            value = matches[path]
        elif self.active_name:
            # Multiple matching paths with an active configuration
            # Search for a single configuration-specific path to use
            specific_paths = [
                path for path in matches.keys()
                if path.endswith(self.active_name)]
            if len(specific_paths) == 1:
                # A single configuration-specific matching path for the key
                path = specific_paths[0]
                value = matches[path]
            else:
                # Multiple configuration-specific matching paths for the key -
                # no other way to determine which value to use
                multiple_matches = True
        else:
            # Multiple matching paths w/o an active configuration for the key -
            # no way to determine which value to use
            multiple_matches = True

        # Report an AvocadoParam-style exception for multiple key matches
        if multiple_matches:
            raise ValueError(
                "Multiple {} leaves contain the key '{}'; {}".format(
                    search_path.pattern, key,
                    ["{}=>{}".format(path, value)
                     for path, value in matches.items()]))

        # Display a AvocadoParams-style logging message for the returned value
        self.log.debug(
            "PARAMS (key=%s, path=%s, default=%s) => %r",
            key, path, default, value)

        return value
