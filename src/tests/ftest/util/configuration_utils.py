#!/usr/bin/python
"""
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
"""
from __future__ import print_function

from ast import literal_eval

from command_utils import ObjectWithParameters, BasicParameter


class ConfigurationParameters(ObjectWithParameters):
    """Defines a configuration with a set of requirement parameters."""

    def __init__(self, namespace, name):
        """Create a ConfigurationParameters object.

        Args:
            name (str): configuration name; used to define param namespace
        """
        super(ConfigurationParameters, self).__init__(namespace + name + "/*")
        self.name = name

        # Define the yaml entries that define the configuration
        self.mem_size = BasicParameter(0, 0)
        self.nvme_size = BasicParameter(0, 0)
        self.scm_size = BasicParameter(0, 0)
        self.network = BasicParameter(None)

    def verify(self, servers):
        """Determine if the list of hosts meet the configuration requirements.

        Args:
            servers (list): list of hosts for which to verify against the
                configuration requirements

        Returns:
            bool: True if all of the hosts meet the configuration requirements

        """
        print("Verifying the {} configuration requirements".format(self.name))
        status = self.check_mem_requirements(servers)
        status &= self.check_nvme_requirements(servers)
        status &= self.check_scm_requirements(servers)
        return status

    def check_mem_requirements(self, servers):
        """Determine if the list of hosts meet the memory requirements.

        Args:
            servers (list): list of hosts for which to verify mem requirements

        Returns:
            bool: True if all of the hosts meet the memory requirements

        """
        print("  Verifying memory size requirements on {}".format(servers))
        return True

    def check_nvme_requirements(self, servers):
        """Determine if the list of hosts meet the nvme requirements.

        Args:
            servers (list): list of hosts for which to verify nvme requirements

        Returns:
            bool: True if all of the hosts meet the nvme requirements

        """
        print("  Verifying nvme size requirements on {}".format(servers))
        return True

    def check_scm_requirements(self, servers):
        """Determine if the list of hosts meet the scm requirements.

        Args:
            servers (list): list of hosts for which to verify scm requirements

        Returns:
            bool: True if all of the hosts meet the scm requirements

        """
        print("  Verifying scm size requirements on {}".format(servers))
        return True


class Configuration(object):
    """Defines a means of obtaining configuration-specific test parameters."""

    def __init__(self, test, debug=True):
        """Initialize a RequirementsManager object.

        Args:
            test (Test): avocado Test object
            debug (bool, optional): display debug messages. Defaults to False.
        """
        self.test = test
        self.namespace = "/run/configurations/"
        self._all_names = []
        self.available_names = []
        self.active_name = None
        self.debug = debug

    def set_config(self, servers):
        """Set the configuration whose requirements pass on the hosts.

        If multiple configuration's requirements pass ofn the specified server
        and client lists of hosts, select the first item in a sorted list.

        Args:
            servers (list): list of servers used to verify each configuration
        """
        # Reset the available and active configuration
        self.available_names = []
        self.active_name = None

        # Optionally display all the paths of which the test is aware
        if self.debug:
            self.test.log.info(
                "PARAMS:\n - %s", "\n - ".join(
                    [str(items) for items in self.test.params.iteritems()]))

        # Find the names of possible configurations from the test yaml, e.g.
        #   "/run/configurations/config1"
        #   "/run/configurations/config2"
        all_paths = set([items[0] for items in self.test.params.iteritems()])
        paths = [path for path in all_paths if path.startswith(self.namespace)]
        self._all_names = [path[len(self.namespace):] for path in paths]

        # Get each configuration's requirements and determine its viablity
        for name in self._all_names:
            config_params = ConfigurationParameters(self.namespace, name)
            config_params.get_params(self.test)
            if config_params.verify(servers):
                # Add valid configurations to the available config list
                self.available_names.append(name)

        # Select the first valid (sorted by name) configuration
        if self.available_names:
            self.active_name = sorted(self.available_names)[0]
            self.test.log.info(
                "Test configuration: %s", self.active_name)
        elif paths:
            self.test.cancel("Test requirements not met!")

    def _log_debug(self, msg, *args, **kwargs):
        """Log a debug message if enabled.

        Args:
            message (str): debug message to log
        """
        if self.debug:
            self.test.log.debug(msg, *args, **kwargs)

    def _get_config_path(self, path, name):
        """Get the configuration-specific version of the path.

        Also remove the wildcard character if one exists.

        Args:
            path (str): path to update
            name (str): configuration name to add to the path

        Returns:
            str: a configuration-specific version of the path

        """
        path_items = path.split("/")
        if path_items[-1] == "*":
            # Replace the wildcard string with the config name
            path_items[-1] = name
        else:
            # Add the config name to the end of the path
            path_items.append(name)
        return "/".join(path_items)

    def get(self, key, path=None, default=None):
        """Get the value associated with key from the parameters.

        Args:
            key (str): the yaml key to search for
            path (str, optional): namespace to search for the key.
                Defaults to None - any relative path.
            default (object, optional): value to assign if key is not found.
                Defaults to None.
        """
        # The default path is any relative path
        if path is None:
            path = "*"
        search_path = path

        # Debug
        self._log_debug(
                "Obtaining value for '%s' in '%s' for the '%s' configuration:",
                key, path, self.active_name)

        # If a configuration is active determine if a configuration-specific
        # version of the specified path exists.  If so use the configuration-
        # specific path instead of the specified path to find the value for the
        # key
        if self.active_name:
            # available_paths = set(
            #     [ipath for ipath, ikey, _ in self.test.params.iteritems()
            #      if ikey == key])
            # search_path = self._get_config_path(path, list(available_paths))

            # Get the configuration-specific version of this path
            config_path = self._get_config_path(path, self.active_name)
            self._log_debug(
                " - Configuration path w/o wildcard: %s", config_path)

            # Deterimine if the configuration-specific path exists for this key
            matching_paths = set(
                [ipath for ipath, ikey, _ in self.test.params.iteritems()
                 if ikey == key and ipath.startswith(config_path)])
            self._log_debug(
                " - Matching configuration paths:    %s", matching_paths)

            # Update the search path to use the configuration-specific path if
            # such a path exists for this key
            if matching_paths and "*" in path:
                search_path = "/".join(config_path.split("/") + ["*"])
            elif matching_paths:
                search_path = config_path

        # Debug
        self._log_debug(" - Search path:                    %s", search_path)

        # Get the value from the config specific path or the specified path
        try:
            value = self.test.params.get(key, search_path, default)

        except ValueError as error:
            # If multiple paths matched then attempt to exclude any paths
            # that match non-active configuraions.
            try:
                # Extract the list of matching paths and their values from the
                # execption string
                path_string = str(error).split(";")[1]
                path_value_list = {
                    item.split("=>")[0]: item.split("=>")[1]
                    for item in list(set(literal_eval(path_string.strip())))}

            except (ValueError, IndexError):
                # Unable to extract multiple paths from the exception - could
                # be a different error.  Report the original error.
                raise error

            # Remove any paths that end in configuration names that are not
            # currently active
            inactive_names = [
                name for name in self._all_names
                if name != self.active_name]
            self._log_debug(
                " - Inactive config names:          %s", inactive_names)
            cfg_path_value_list = {
                key: value for key, value in path_value_list.items()
                if key.split("/")[-1] not in inactive_names}
            self._log_debug(
                " - Filtered paths:                 %s", cfg_path_value_list)

            # If a single path remains use its value
            if len(cfg_path_value_list) == 1:
                value = list(cfg_path_value_list.values())[0]
                self.test.log.debug(
                    "PARAMS (key=%s, path=%s, default=%s) => %r",
                    key, list(cfg_path_value_list.keys())[0], default, value)
            else:
                # Raise the error
                raise error

        return value
