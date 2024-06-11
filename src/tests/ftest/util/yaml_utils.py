"""
(C) Copyright 2020-2024 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
from collections import OrderedDict

import yaml
from ClusterShell.NodeSet import NodeSet
# pylint: disable=import-error,no-name-in-module
from util.data_utils import dict_extract_values, list_flatten, list_unique


class YamlException(BaseException):
    """Base exception for this module."""


def get_test_category(test_file):
    """Get a category for the specified test using its path and name.

    Args:
        test_file (str): the test python file

    Returns:
        str: concatenation of the test path and base filename joined by dashes

    """
    file_parts = os.path.split(test_file)
    return "-".join([os.path.splitext(os.path.basename(part))[0] for part in file_parts])


def get_yaml_data(yaml_file):
    """Get the contents of a yaml file as a dictionary.

    Removes any mux tags and ignores any other tags present.

    Args:
        yaml_file (str): yaml file to read

    Raises:
        YamlException: if an error is encountered reading the yaml file

    Returns:
        dict: the contents of the yaml file

    """
    if not os.path.isfile(yaml_file):
        raise YamlException(f"File not found: {yaml_file}")

    class DaosLoader(yaml.SafeLoader):
        """Helper class for parsing avocado yaml files."""

        def forward_mux(self, node):
            """Pass on mux tags unedited."""
            return self.construct_mapping(node)

        def ignore_unknown(self, node):  # pylint: disable=no-self-use,unused-argument
            """Drop any other tag."""
            return None

    DaosLoader.add_constructor('!mux', DaosLoader.forward_mux)
    DaosLoader.add_constructor(None, DaosLoader.ignore_unknown)

    with open(yaml_file, "r", encoding="utf-8") as open_file:
        try:
            return yaml.load(open_file.read(), Loader=DaosLoader)
        except yaml.YAMLError as error:
            raise YamlException(f"Error reading {yaml_file}") from error


class YamlUpdater():
    """A class for updating placeholders in test yaml files."""

    # List of (yaml key, attribute name, val_type)
    YAML_KEYS = [
        ("test_servers", "_servers", (list, int, str)),
        ("test_clients", "_clients", (list, int, str)),
        ("bdev_list", "_storage", list),
        ("timeout", "_timeout", int),
        ("timeouts", "_timeout", dict),
        ("clush_timeout", "_timeout", int),
        ("ior_timeout", "_timeout", int),
        ("job_manager_timeout", "_timeout", int),
        ("pattern_timeout", "_timeout", int),
        ("pool_query_timeout", "_timeout", int),
        ("pool_query_delay", "_timeout", int),
        ("rebuild_timeout", "_timeout", int),
        ("rebuild_query_delay", "_timeout", int),
        ("srv_timeout", "_timeout", int),
        ("storage_prepare_timeout", "_timeout", int),
        ("storage_format_timeout", "_timeout", int),
    ]

    def __init__(self, logger, servers, clients, storage, timeout, override, verbose):
        """Initialize a YamlUpdater object.

        Args:
            logger (logger): logger for the messages produced by this class
            servers (NodeSet): set of hosts to use as servers in the test yaml
            clients (NodeSet): set of hosts to use as clients in the test yaml
            storage (str): updated storage information to apply to the test yaml
            timeout (int): multiplier to apply to any timeouts specified in the test yaml
            verbose (int): verbosity level
        """
        self.log = logger
        self._servers = servers
        self._clients = clients
        self._storage = storage
        self._timeout = timeout
        self._override = override
        self._verbose = verbose

    @property
    def placeholder_updates(self):
        """Do placeholder replacements exist for updating the test yaml.

        Returns:
            bool: whether or not placeholder replacements exist
        """
        return self._servers or self._storage or self._timeout

    def update(self, yaml_file, yaml_dir):
        """Update the placeholders in the specified test yaml file.

        Args:
            yaml_file (str): test yaml file to update
            yaml_dir (str): directory in which to write the updated test yaml file

        Raises:
            YamlException: if any placeholders are found without a replacement

        Returns:
            str: the updated test yaml file or None if no updates were made

        """
        self.log.debug("-" * 80)
        replacements = self.get_replacements(yaml_file)
        return self.apply_changes(yaml_file, yaml_dir, replacements)

    def get_replacements(self, yaml_file):
        """Determine the replacement values for the placeholders in the test yaml.

        Args:
            yaml_file (str): the test yaml file

        Raises:
            YamlException: if there was a problem replacing any of the placeholders

        Returns:
            dict: a dictionary of existing test yaml entry keys and their replacement values

        """
        replacements = {}
        if not self.placeholder_updates:
            return replacements

        # Find the test yaml keys and values that match the replaceable fields
        yaml_data = get_yaml_data(yaml_file)
        self.log.debug("Detected yaml data: %s", yaml_data)
        placeholder_data = {}
        for key, _, val_type in self.YAML_KEYS:
            # Get the unique values with lists flattened
            values = list_unique(list_flatten(dict_extract_values(yaml_data, [key], val_type)))
            if values:
                # Use single value if list only contains 1 element
                placeholder_data[key] = values if len(values) > 1 else values[0]

        # Generate a list of values that can be used as replacements
        replacement_data = OrderedDict()
        for key, attr_name, _ in self.YAML_KEYS:
            args_value = getattr(self, attr_name)
            if isinstance(args_value, NodeSet):
                replacement_data[key] = list(args_value)
            elif isinstance(args_value, str):
                replacement_data[key] = args_value.split(",")
            elif args_value:
                replacement_data[key] = [args_value]
            else:
                replacement_data[key] = None

        # Assign replacement values for the test yaml entries to be replaced
        placeholder_keys = [yaml_key[0] for yaml_key in self.YAML_KEYS]
        self.log.debug("Detecting replacements for %s in %s", placeholder_keys, yaml_file)
        self.log.debug("  Placeholder data: %s", placeholder_data)
        self.log.debug("  Replacement data:  %s", dict(replacement_data))

        node_mapping = {}
        for key, replacement in replacement_data.items():
            # If the user did not provide a specific list of replacement test_clients values, use
            # the remaining test_servers values to replace test_clients placeholder values
            if key == "test_clients" and not replacement:
                replacement = replacement_data["test_servers"]

            if key not in placeholder_data:
                if self._verbose > 1:
                    self.log.debug("  - No '%s' placeholder specified in the test yaml", key)
                continue

            if not replacement:
                if self._verbose > 1:
                    self.log.debug(
                        "  - No replacement value for the '%s' placeholder: %s",
                        key, placeholder_data[key])
                continue

            # Replace test yaml keys that were:
            #   - found in the test yaml
            #   - have a user-specified replacement
            if key.startswith("test_"):
                # The entire server/client test yaml list entry is replaced by a new test yaml list
                # entry, e.g.
                #   '  test_servers: server-[1-2]' --> '  test_servers: wolf-[10-11]'
                #   '  test_servers: 4'            --> '  test_servers: wolf-[10-13]'
                self._get_host_replacement(
                    replacements, placeholder_data, key, replacement, node_mapping)

            elif key == "bdev_list":
                # Individual bdev_list NVMe PCI addresses in the test yaml file are replaced with
                # the new NVMe PCI addresses in the order they are found, e.g.
                #   0000:81:00.0 --> 0000:12:00.0
                self._get_storage_replacement(replacements, placeholder_data, key, replacement)

            else:
                # Timeouts - replace the entire timeout entry (key + value) with the same key with
                # its original value multiplied by the user-specified value, e.g.
                #   timeout: 60 -> timeout: 600
                self._get_timeout_replacement(replacements, placeholder_data, key, replacement)

        # Display the replacement values
        for value, replacement in list(replacements.items()):
            self.log.debug("  - Replacement: %s -> %s", value, replacement)

        return replacements

    def _get_host_replacement(self, replacements, placeholder_data, key, replacement, node_mapping):
        """Replace the server or client placeholders.

        Args:
            replacements (dict): dictionary in which to add replacements for test yaml entries
            placeholder_data (dict): test yaml values requesting replacements
            key (str): test yaml entry key
            replacement (list): available values to use as replacements for the test yaml entries
            node_mapping (dict): dictionary of nodes and their replacement values

        Raises:
            YamlException: if there was a problem replacing any of the placeholders

        """
        if not isinstance(placeholder_data[key], list):
            placeholder_data[key] = [placeholder_data[key]]

        for placeholder in placeholder_data[key]:
            replacement_nodes = NodeSet()
            try:
                # Replace integer placeholders with the number of nodes from the user provided list
                # equal to the quantity requested by the test yaml
                quantity = int(placeholder)
                if self._override and self._clients:
                    # When individual lists of server and client nodes are provided with the
                    # override flag set use the full list of nodes specified by the
                    # test_server/test_client arguments
                    quantity = len(replacement)
                elif self._override:
                    self.log.warning(
                        "Warning: In order to override the node quantity a "
                        "'--test_clients' argument must be specified: %s: %s",
                        key, placeholder)
                for _ in range(quantity):
                    try:
                        replacement_nodes.add(replacement.pop(0))
                    except IndexError as error:
                        # Not enough nodes provided for the replacement
                        message = f"Not enough '{key}' placeholder replacements specified"
                        self.log.error("  - %s; required: %s", message, quantity)
                        raise YamlException(message) from error

            except ValueError:
                try:
                    # Replace clush-style placeholders with nodes from the user provided list using
                    # a mapping so that values used more than once yield the same replacement
                    for node in NodeSet(placeholder):
                        if node not in node_mapping:
                            try:
                                node_mapping[node] = replacement.pop(0)
                            except IndexError as error:
                                # Not enough nodes provided for the replacement
                                if not self._override:
                                    message = f"Not enough '{key}' placeholder replacements remain"
                                    self.log.error(
                                        "  - %s; required: %s", message, NodeSet(placeholder))
                                    raise YamlException(message) from error
                                break
                            self.log.debug(
                                "  - %s replacement node mapping: %s -> %s",
                                key, node, node_mapping[node])
                        replacement_nodes.add(node_mapping[node])

                except TypeError as error:
                    # Unsupported format
                    message = f"Unsupported placeholder format: {placeholder}"
                    self.log.error(message)
                    raise YamlException(message) from error

            hosts_key = r":\s+".join([key, str(placeholder)])
            hosts_key = hosts_key.replace("[", r"\[")
            hosts_key = hosts_key.replace("]", r"\]")
            if replacement_nodes:
                replacements[hosts_key] = ": ".join([key, str(replacement_nodes)])
            else:
                replacements[hosts_key] = None

    @staticmethod
    def _get_storage_replacement(replacements, placeholder_data, key, replacement):
        """Add replacements for the storage entries in the test yaml file.

        Args:
            replacements (dict): dictionary in which to add replacements for test yaml entries
            placeholder_data (dict): test yaml values requesting replacements
            key (str): test yaml entry key
            replacement (list): available values to use as replacements for the test yaml entries
        """
        for placeholder in placeholder_data[key]:
            bdev_key = f"\"{placeholder}\""
            if bdev_key in replacements:
                continue
            try:
                replacements[bdev_key] = f"\"{replacement.pop(0)}\""
            except IndexError:
                replacements[bdev_key] = None

    def _get_timeout_replacement(self, replacements, placeholder_data, key, replacement):
        """Add replacements for the timeout entries in the test yaml file.

        Args:
            replacements (dict): dictionary in which to add replacements for test yaml entries
            placeholder_data (dict): test yaml values requesting replacements
            key (str): test yaml entry key
            replacement (list): available values to use as replacements for the test yaml entries
        """
        if isinstance(placeholder_data[key], int):
            timeout_key = r":\s+".join([key, str(placeholder_data[key])])
            timeout_new = max(1, round(placeholder_data[key] * replacement[0]))
            replacements[timeout_key] = ": ".join([key, str(timeout_new)])
            self.log.debug(
                "  - Timeout adjustment (x %s): %s -> %s",
                replacement, timeout_key, replacements[timeout_key])

        elif isinstance(placeholder_data[key], dict):
            for timeout_test, timeout_val in list(placeholder_data[key].items()):
                timeout_key = r":\s+".join([timeout_test, str(timeout_val)])
                timeout_new = max(1, round(timeout_val * replacement[0]))
                replacements[timeout_key] = ": ".join(
                    [timeout_test, str(timeout_new)])
                self.log.debug(
                    "  - Timeout adjustment (x %s): %s -> %s",
                    replacement, timeout_key, replacements[timeout_key])

    def apply_changes(self, yaml_file, yaml_dir, replacements):
        """Apply placeholder replacements in a new copy of the test yaml.

        Args:
            yaml_file (str): test yaml file to update
            yaml_dir (str): directory in which to write the updated test yaml file
            replacements (dict): dictionary in which to add replacements for test yaml entries

        Raises:
            YamlException: if any placeholders are found without a replacement

        Returns:
            str: the updated test yaml file or None if no updates were made

        """
        if not replacements:
            return None

        # Read in the contents of the yaml file to retain the !mux entries
        self.log.debug("Reading %s", yaml_file)
        with open(yaml_file, encoding="utf-8") as yaml_buffer:
            yaml_data = yaml_buffer.read()

        # Apply the placeholder replacements
        missing_replacements = []
        self.log.debug("Modifying contents: %s", yaml_file)
        for key in sorted(replacements):
            value = replacements[key]
            if value:
                # Replace the host entries with their mapped values
                self.log.debug("  - Replacing: %s --> %s", key, value)
                yaml_data = re.sub(key, value, yaml_data)
            else:
                # Keep track of any placeholders without a replacement value
                self.log.debug("  - Missing:   %s", key)
                missing_replacements.append(key)
        if missing_replacements:
            # Report an error for all of the placeholders w/o a replacement
            self.log.error(
                "Error: Placeholders missing replacements in %s:\n  %s",
                yaml_file, ", ".join(missing_replacements))
            raise YamlException(f"Error: Placeholders missing replacements in {yaml_file}")

        # Write the modified yaml file into a temporary file.  Use the path to
        # ensure unique yaml files for tests with the same filename.
        yaml_name = get_test_category(yaml_file)
        yaml_file = os.path.join(yaml_dir, f"{yaml_name}.yaml")
        self.log.debug("Creating copy: %s", yaml_file)
        with open(yaml_file, "w", encoding="utf-8") as yaml_buffer:
            yaml_buffer.write(yaml_data)

        return yaml_file
