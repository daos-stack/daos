"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import errno
import os
import re
import time

from ClusterShell.NodeSet import NodeSet
# pylint: disable=import-error,no-name-in-module
from util.run_utils import run_remote

# Order here is used to select default provider in environment_utils
SUPPORTED_PROVIDERS = (
    "ofi+cxi",
    "ofi+verbs;ofi_rxm",
    "ucx+dc_x",
    "ucx+ud_x",
    "ofi+tcp",
    "ofi+tcp;ofi_rxm",
    "ofi+opx"
)
PROVIDER_ALIAS = {
    "ofi+verbs": "ofi+verbs;ofi_rxm"
}


class NetworkException(Exception):
    """Exception for network_utils methods."""


class NetworkDevice():
    """A class to represent the information of a network device."""

    def __init__(self, host, name, device, port, provider, numa):
        """Initialize the network device data object."""
        self.host = host
        self.name = name
        self.device = device
        self.port = port
        self.provider = provider
        self.numa = numa

    def __repr__(self):
        """Overwrite to display formatted devices."""
        return self.__str__()

    def __str__(self):
        """Overwrite to display formatted devices."""
        settings = [f"{key}={getattr(self, key, None)}" for key in self.__dict__]
        return f"NetworkDevice({', '.join(settings)})"

    def __ne__(self, other):
        """Override the default not-equal implementation."""
        return not self.__eq__(other)

    def __eq__(self, other):
        """Override the default implementation to compare devices."""
        status = True
        if not isinstance(other, NetworkDevice):
            return False
        for key in self.__dict__:
            try:
                status &= str(getattr(self, key)) == str(getattr(other, key))
            except AttributeError:
                return False
        return status

    @property
    def domain(self):
        """Get the domain for this network device.

        Returns:
            str: the domain for this network device

        """
        if "ucx" in self.provider:
            return ":".join([self.device, self.port])
        return self.device


def get_active_network_interfaces(logger, hosts, verbose=True):
    """Get all active network interfaces on the hosts.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to find active interfaces
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of interface keys and NodeSet values on which they were found
    """
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    operstate = os.path.join(net_path, "*", "operstate")
    command = " | ".join([f"grep -l 'up' {operstate}", "grep -Ev '/(lo|bond)/'", "sort"])
    result = run_remote(logger, hosts, command, verbose)

    # Populate a dictionary of active interfaces with a NodSet of hosts on which it was found
    active_interfaces = {}
    for data in result.output:
        if not data.passed:
            continue
        for line in data.stdout:
            try:
                interface = line.split("/")[-2]
                if interface not in active_interfaces:
                    active_interfaces[interface] = NodeSet()
                active_interfaces[interface].update(data.hosts)
            except IndexError:
                pass

    return active_interfaces


def get_common_interfaces(logger, hosts, verbose=True):
    """Get the active interfaces that exists on all the hosts.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to find active interfaces
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        list: a list of available NetworkDevice objects common to all hosts
    """
    # Find any active network interfaces on the server or client hosts
    interfaces_per_host = get_active_network_interfaces(logger, hosts, verbose)

    # From the active interface dictionary find all the interfaces that are common to all hosts
    logger.info("Active network interfaces detected:")
    common_interfaces = []
    for interface, node_set in interfaces_per_host.items():
        logger.info("  - %-8s on %s (Common=%s)", interface, node_set, node_set == hosts)
        if node_set == hosts:
            common_interfaces.append(interface)

    return common_interfaces


def get_interface_speeds(logger, hosts, interface, verbose=True):
    """Get the speeds of this network interface on each host.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to detect the interface speed
        interface (str): interface for which to obtain the speed
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of interface speed keys and NodeSet values on which they were detected
    """
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    command = f"cat {os.path.join(net_path, interface, 'speed')}"
    result = run_remote(logger, hosts, command, verbose)

    # Populate a dictionary of interface speeds with a NodSet of hosts on which it was detected
    interface_speeds = {}
    for data in result.output:
        if not data.passed:
            continue
        for line in data.stdout:
            try:
                speed = int(line.strip())
            except IOError as io_error:
                # KVM/Qemu/libvirt returns an EINVAL
                if io_error.errno == errno.EINVAL:
                    speed = 1000
            except ValueError:
                # Any line not containing a speed (integer)
                continue
            if speed not in interface_speeds:
                interface_speeds[speed] = NodeSet()
            interface_speeds[speed].update(data.hosts)

    return interface_speeds


def get_interface_numa_node(logger, hosts, interface, verbose=True):
    """Get the NUMA node ID of this network interface on each host.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to detect the NUMA node
        interface (str): interface for which to obtain the NUMA node
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of NUMA node ID keys and NodeSet values on which they were detected
    """
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    command = f"cat {os.path.join(net_path, interface, 'device', 'numa_node')}"
    result = run_remote(logger, hosts, command, verbose)

    # Populate a dictionary of numa node IDs with a NodSet of hosts on which it was detected
    numa_nodes = {}
    for data in result.output:
        if not data.passed:
            continue
        for line in data.stdout:
            try:
                numa_node = int(line.strip())
            except ValueError:
                continue
            if numa_node not in numa_nodes:
                numa_nodes[numa_node] = NodeSet()
            numa_nodes[numa_node].update(data.hosts)

    return numa_nodes


def get_interface_device_name(logger, hosts, interface, verbose=True):
    """Get the device name of this network interface on each host.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to detect the device name
        interface (str): interface for which to obtain the device name
        verbose (bool, optional): display command details. Defaults to True

    Returns:
        dict: a dictionary of device name keys and NodeSet values on which they were detected
    """
    device_names = {}
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    device_dirs = ["infiniband", "cxi"]

    while not device_names and device_dirs:
        device_dir = device_dirs.pop(0)
        command = f"ls -1 {os.path.join(net_path, interface, 'device', device_dir)}"
        result = run_remote(logger, hosts, command, verbose)

        # Populate a dictionary of IB names with a NodSet of hosts on which it was detected
        for data in result.output:
            if not data.passed:
                continue
            device_name_list = []
            for line in data.stdout:
                match = re.findall(r"([A-Za-z0-9;_+]+)", line)
                if len(match) == 1:
                    device_name_list.append(match[0])
            if device_name_list:
                device_names[",".join(device_name_list)] = NodeSet.fromlist(data.hosts)

    return device_names


def get_hg_info(logger, hosts, filter_provider=None, filter_device=None, verbose=True):
    """Get the HG provider information from the specified hosts.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts from which to gather the information
        filter_provider (list, optional): list of supported providers to filter by.
            Defaults to None.
        filter_device (list, optional): list of supported devices to filter by.
            Defaults to None.
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of interface keys with a dictionary value of a comma-separated string of
            providers key with a NodeSet value where the providers where detected.
    """
    command = "hg_info"
    result = run_remote(logger, hosts, command, verbose=verbose, stderr=True)
    providers = {}
    if result.passed:
        # Find all supported providers
        for data in result.output:
            if not data.stdout:
                continue
            # Skip over the header
            without_header = re.findall(
                r'^-+\n^.*\n^-+\n(.*)', '\n'.join(data.stdout), re.MULTILINE | re.DOTALL)[0]
            # Convert:
            # <Class>  <Protocol>  <Device>
            # To {device: set(providers)}
            device_providers = {}
            class_protocol_device = re.findall(
                r'(\S+) +([\S]+) +([\S]+)$', without_header, re.MULTILINE)
            for _class, protocol, device in class_protocol_device:
                if filter_device and device not in filter_device:
                    continue
                provider = f"{_class}+{protocol}"
                if filter_provider and provider not in filter_provider:
                    continue
                if device not in device_providers:
                    device_providers[device] = set()
                device_providers[device].add(provider)

            for device, provider_set in device_providers.items():
                if device not in providers:
                    providers[device] = {}
                provider_key = ",".join(provider_set)
                if provider_key not in providers[device]:
                    providers[device][provider_key] = NodeSet()
                providers[device][provider_key].update(data.hosts)

    return providers


def get_ucx_info(logger, hosts, supported=None, verbose=True):
    """Get the UCX provider information from the specified hosts.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts from which to gather the information
        supported (list, optional): list of supported providers when if provided will limit the
            inclusion to only those providers specified. Defaults to None.
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of interface keys with a dictionary value of a comma-separated string of
            providers key with a NodeSet value where the providers where detected.
    """
    result = run_remote(logger, hosts, "ucx_info -d", verbose)

    # Populate a dictionary of interfaces with a list of provider lists and NodSet of hosts on which
    # the providers were detected.
    providers = {}
    for data in result.output:
        if not data.passed:
            continue

        # Find all the transport, device, and type pairings. The ucx_info output reports these on
        # separate lines so when processing the re matches ensure each device is preceded by a
        # provider.
        interface_providers = {}
        matches = re.findall(r"(Transport|Device):\s+([A-Za-z0-9;_+]+)", "\n".join(data.stdout))
        while matches:
            transport = list(matches.pop(0))
            if transport[0] == "Transport" and matches[0][0] == "Device":
                transport.pop(0)
                device = list(matches.pop(0))
                device.pop(0)

                # A transport and device must be specified
                if not transport or not device:
                    continue

                # Add 'ucx+' to the provider and replace 'mlx[0-9]' with 'x'
                transport = [
                    "+".join(["ucx", re.sub(r"mlx[0-9]+", "x", item)]) for item in transport]

                # Only include supported providers if a supported list is provided
                if supported and transport[0] not in supported:
                    continue

                if device[0] not in interface_providers:
                    interface_providers[device[0]] = set()
                interface_providers[device[0]].update(transport)

        for interface, provider_set in interface_providers.items():
            if interface not in providers:
                providers[interface] = {}
            provider_key = ",".join(list(provider_set))
            if provider_key not in providers[interface]:
                providers[interface][provider_key] = NodeSet()
            providers[interface][provider_key].update(data.hosts)

    return providers


def get_interface_providers(interface, provider_data):
    """Get the providers supported by this interface.

    Args:
        interface (str): interface for which to obtain the InfiniBand name
        provider_data (dict): output from get_hg_info() or get_ucx_info()

    Returns:
        dict: a dictionary of comma-separated strings of providers keys and NodeSet values on which
            they were detected

    """
    providers = {}
    if interface in provider_data:
        for provider, node_set in provider_data[interface].items():
            if provider not in providers:
                providers[provider] = NodeSet()
            providers[provider].update(node_set)

    return providers


def get_fastest_interface(logger, hosts, verbose=True):
    """Get the fastest active interface common to all hosts.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to find the fastest interfaces
        verbose (bool, optional): display command details. Defaults to True.

    Raises:
        NetworkException: if there is an error detecting the fastest active interface

    Returns:
        str: the fastest active interface common to all hosts specified
    """
    common_interfaces = get_common_interfaces(logger, hosts, verbose)

    # Find the speed of each common active interface in order to be able to choose the fastest
    interface_speeds = {}
    for interface in common_interfaces:
        detected_speeds = get_interface_speeds(logger, hosts, interface, verbose)
        speed_list = []
        for speed, node_set in detected_speeds.items():
            if node_set == hosts:
                # Only include detected homogeneous interface speeds
                speed_list.append(speed)
        if speed_list:
            interface_speeds[interface] = min(speed_list)

    logger.info("Active network interface speeds on %s:", hosts)
    available_interfaces = {}
    for interface in sorted(interface_speeds):
        logger.info("  - %-8s (speed: %6s)", interface, interface_speeds[interface])

        # Only include the first active interface (as determined by alphabetic sort) for each speed
        if interface_speeds[interface] not in available_interfaces:
            available_interfaces[interface_speeds[interface]] = interface

    logger.info("Available interfaces on %s: %s", hosts, available_interfaces)
    try:
        # Select the fastest active interface available by sorting the speed
        interface = available_interfaces[sorted(available_interfaces)[-1]]
    except IndexError as error:
        raise NetworkException("Error obtaining a default interface!") from error

    logger.info("Fastest interface detected on %s: %s", hosts, interface)
    return interface


def get_common_provider(logger, hosts, interface, supported=None, verbose=True):
    """Get the list of providers supported by the interface on every host.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to find the provider information
        interface (str): interface for which to obtain the providers
        supported (list, optional): list of supported providers when if provided will limit the
            inclusion to only those providers specified. Defaults to None.
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        list: a list of providers supported by the interface on every host
    """
    hg_info = get_hg_info(logger, hosts, filter_provider=supported, verbose=verbose)
    providers = get_interface_providers(interface, hg_info)
    for dev_name in get_interface_device_name(logger, hosts, interface, verbose):
        providers.update(get_interface_providers(dev_name, hg_info))

    # Only include the providers supported by the interface on all of the hosts
    common_providers = set()
    if providers:
        logger.debug("Detected providers:")
    for provider, node_set in providers.items():
        logger.debug("  %4s: %s", provider, node_set)
        if node_set == hosts:
            common_providers.update(provider.split(","))

    return list(common_providers)


def get_network_information(logger, hosts, supported=None, verbose=True):
    """Get the network device information on the hosts specified.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to find the network information
        supported (list, optional): list of supported providers when if provided will limit the
            inclusion to only those providers specified. Defaults to None.
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        list: a list of NetworkDevice objects identifying the network devices on each host
    """
    network_devices = []

    ofi_info = get_hg_info(logger, hosts, filter_provider=supported, verbose=verbose)
    ucx_info = get_ucx_info(logger, hosts, supported, verbose)

    interfaces = get_active_network_interfaces(logger, hosts, verbose)
    for host in hosts:
        for interface, node_set in interfaces.items():
            if host not in node_set:
                continue
            kwargs = {"host": host, "name": interface, "port": 1}
            data_gather = {
                "device": get_interface_device_name(logger, node_set, interface, verbose),
                "provider": get_interface_providers(interface, ofi_info),
                "numa": get_interface_numa_node(logger, node_set, interface, verbose),
            }
            for dev_name in data_gather["device"]:
                for add_on in ([], ["1"]):
                    device = ":".join([dev_name] + add_on)
                    data_gather["provider"].update(get_interface_providers(device, ofi_info))
                    data_gather["provider"].update(get_interface_providers(device, ucx_info))
            for key, data in data_gather.items():
                kwargs[key] = []
                for item, item_node_set in data.items():
                    if node_set == item_node_set:
                        kwargs[key].append(item)
                if kwargs[key]:
                    kwargs[key] = ",".join([str(item) for item in kwargs[key]])
                else:
                    kwargs[key] = None

            if kwargs["provider"] is not None:
                for item in kwargs["provider"].split(","):
                    these_kwargs = kwargs.copy()
                    these_kwargs["provider"] = item
                    network_devices.append(NetworkDevice(**these_kwargs))

    return network_devices


class NetworkInterface():
    "A class representing a network interface."

    def __init__(self, name, hosts, execute=True):
        """Initialize the network interface object.

        Args:
            name (str): the network interface name, e.g. ib0
            hosts (NodeSet): hosts on which the interface will be updated.
            execute (bool, optional): should the command be executed or just displayed. Defaults to
                True.
        """
        self.__name = name
        self.__hosts = hosts
        self.__execute = execute
        # Assume device is down until bring_up() is successful when commands will be executed
        self.__up = not self.__execute

    def bring_up(self, logger):
        """Bring up the network interface.

        Args:
            logger (Logger): logger for the messages produced by this method

        Returns:
            list: any errors detected setting the link state of the interface
        """
        return self.__set_state(logger, 'up')

    def bring_down(self, logger):
        """Bring down the network interface.

        Args:
            logger (Logger): logger for the messages produced by this method

        Returns:
            list: any errors detected setting the link state of the interface
        """
        return self.__set_state(logger, 'down')

    def restore(self, logger):
        """Bring up the network interface only if it is down.

        Args:
            logger (Logger): logger for the messages produced by this method

        Returns:
            list: any errors detected setting the link state of the interface
        """
        if self.__up:
            logger.debug(f'Interface {self.__name} is already up and does not need to be restored')
            return []
        return self.bring_up(logger)

    def __set_state(self, logger, state):
        """Set the network interface state.

        Args:
            logger (Logger): logger for the messages produced by this method
            state (str): "up" or "down".

        Returns:
            list: any errors detected setting the link state of the interface
        """
        errors = []
        command = f'sudo -n ip link set {self.__name} {state}'
        if self.__execute:
            result = run_remote(logger, self.__hosts, command)
            if not result.passed:
                errors.append(f'Error setting {self.__name} {state} on {result.failed_hosts}')
            else:
                self.__up = bool(state == 'up')
        else:
            logger.debug('>>> Run "%s" on %s within 20 seconds <<<', command, self.__hosts)
            time.sleep(20)
        return errors
