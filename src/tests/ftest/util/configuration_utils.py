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
from ast import literal_eval
from logging import getLogger
import re

from ClusterShell.NodeSet import NodeSet

from command_utils import ObjectWithParameters, BasicParameter
from general_utils import run_task


class ConfigurationParameters(ObjectWithParameters):
    """Defines a configuration with a set of requirement parameters."""

    def __init__(self, namespace, name, timeout=120):
        """Create a ConfigurationParameters object.

        Args:
            name (str): configuration name; used to define param namespace
        """
        super(ConfigurationParameters, self).__init__(namespace + name + "/*")
        self.name = name
        self.timeout = timeout
        self.log = getLogger(__name__)

        # Define the yaml entries that define the configuration
        self.mem_size = BasicParameter(0, 0)
        self.nvme_size = BasicParameter(0, 0)
        self.scm_size = BasicParameter(0, 0)
        self.network = BasicParameter(None)

    def _check_size_requirements(self, servers, value, command, text, error):
        """Determine if the list of hosts meet the specified requirement.

        Args:
            servers (list): list of hosts for which to verify the requirement
            value (int): requirment size
            command (str): command used to obtain the value on each server
            text (str): requirement identification string
            error (str): requirement error string

        Returns:
            bool: True if all of the hosts meet the specified requirement

        """
        # Only check for the requirement if a minimum size is requested
        status = value == 0
        if not status:
            self.log.info(
                "  Verifying %s size is at least %s on %s",
                text, value, servers)
            # Find the requirement on the specified servers
            task = run_task(servers, command, self.timeout)

            # Create a list of NodeSets with the same return code
            data = {code: hosts for code, hosts in task.iter_retcodes()}

            # Multiple return codes or a single non-zero return code
            # indicate at least one error detecting the requirement
            status = len(data) == 1 and 0 in data
            if not status:
                # Report the errors
                messages = []
                for code, hosts in data.items():
                    if code != 0:
                        output_data = list(task.iter_buffers(hosts))
                        if len(output_data) == 0:
                            messages.append(
                                "{}: rc={}, command=\"{}\"".format(
                                    NodeSet.fromlist(hosts), code, command))
                        else:
                            for output, o_hosts in output_data:
                                lines = str(output).splitlines()
                                info = "rc={}{}".format(
                                    code,
                                    ", {}".format(output) if len(lines) < 2 else
                                    "\n  {}".format("\n  ".join(lines)))
                                messages.append(
                                    "{}: {}".format(
                                        NodeSet.fromlist(o_hosts), info))
                self.log.error(
                    "    %s on the following hosts:\n      %s",
                    error, "\n      ".join(messages))
            else:
                # The command completed successfully on all servers.  Verify
                # that the current value meets or exceeds the required value.
                for output, hosts in task.iter_buffers(data[0]):
                    # Find the maximum size of the all the devices reported by
                    # this group of hosts as only one needs to meet the minimum
                    nodes = NodeSet.fromlist(hosts)
                    try:
                        int_host_values = [
                            int(line.split()[0])
                            for line in str(output).splitlines()]
                        max_host_value = max(int_host_values)
                        status &= max_host_value >= value
                    except (IndexError, ValueError):
                        self.log.error(
                            "    Unable to verify the maximum %s size due to "
                            "unexpected output:\n      %s",
                            text, "\n      ".join(str(output).splitlines()))
                        max_host_value = "[ERROR]"
                        status = False
                    self.log.debug(
                        "    %s: verifying the maximum %s size meets the "
                        "requirement: %s >= %s: %s",
                        str(nodes), text, max_host_value, value, str(status))
                    if not status:
                        break
        else:
            self.log.info(
                "  No %s size requirement for %s", text, servers)

        return status

    def verify(self, servers):
        """Determine if the list of hosts meet the configuration requirements.

        Args:
            servers (list): list of hosts for which to verify against the
                configuration requirements

        Returns:
            bool: True if all of the hosts meet the configuration requirements

        """
        self.log.info("Verifying the %s configuration requirements", self.name)
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
        # Get the total available memory in bytes
        cmd = r"free -b | sed -En 's/Mem:\s+([0-9]+).*/\1/p'"
        text = "memory"
        error = "Error obtaining total memory size"
        value = int(self.mem_size.value)
        return self._check_size_requirements(servers, value, cmd, text, error)

    def check_nvme_requirements(self, servers):
        """Determine if the list of hosts meet the nvme requirements.

        Args:
            servers (list): list of hosts for which to verify nvme requirements

        Returns:
            bool: True if all of the hosts meet the nvme requirements

        """
        # Find the byte capacity of the nvme devices bound to the kernel driver
        cmd = "lsblk -b -o SIZE,NAME | grep nvme"
        text = "NVMe"
        error = "No NVMe drives bound to the kernel driver detected"
        value = int(self.nvme_size.value)
        return self._check_size_requirements(servers, value, cmd, text, error)

    def check_scm_requirements(self, servers):
        """Determine if the list of hosts meet the scm requirements.

        Args:
            servers (list): list of hosts for which to verify scm requirements

        Returns:
            bool: True if all of the hosts meet the scm requirements

        """
        cmds = [
            "sudo -n ipmctl show -units B -memoryresources",
            r"sed -En 's/^Capacity\=([0-9+]).*/\1/p'",
        ]
        text = "SCM"
        error = "No SCM devices detected"
        value = int(self.scm_size.value)
        return self._check_size_requirements(
            servers, value, " | ".join(cmds), text, error)


class Configuration(object):
    """Defines a means of obtaining configuration-specific test parameters."""

    def __init__(self, test_params, debug=True):
        """Initialize a RequirementsManager object.

        Args:
            test_params (AvocadoParams): avocado Test parameters
            debug (bool, optional): display debug messages. Defaults to False.
        """
        self.log = getLogger(__name__)
        self.namespace = "/run/configurations/"
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
        """Set the object parameters based upon the test paramemeter items.

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
            # to find the requirments for each configuration
            return True
        else:
            # Otherwise only include paths that are not configuration-specific
            # or are specific to the active configuration
            return path.split("/")[-1] not in self._inactive_names

    def _set_available_names(self, test, hosts):
        """Set the list of available configuration names.

        Configuration names are listed as available if all of the specified
        hosts meet the configuration's requirements.

        Args:
            test (Test): avocado Test object
            hosts (list): list of hosts used to verify each configuration's
                requirements to determine if the configuration is valid
        """
        self.available_names = []
        for name in self._all_names:
            # Get each configuration's requirements and determine its viablity
            config_params = ConfigurationParameters(self.namespace, name)
            config_params.get_params(test)
            if config_params.verify(hosts):
                # Add valid configurations to the available config list
                self.available_names.append(name)

    def set_config(self, test, hosts):
        """Set the configuration whose requirements pass on the hosts.

        If multiple configuration's requirements pass on the specified list of
        hosts, select the first item in a sorted list.

        Args:
            test (Test): avocado Test object
            hosts (list): list of hosts used to verify each configuration

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
        self._set_available_names(test, hosts)

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

    def _log_debug(self, msg, *args, **kwargs):
        """Log a debug message if enabled.

        Args:
            message (str): debug message to log
        """
        if self.debug:
            self.log.debug(msg, *args, **kwargs)

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
            re_path += "$"
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

        # Report an AvocadoParam-style exception for multple key matches
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
