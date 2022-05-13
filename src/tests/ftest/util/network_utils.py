#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import errno
from logging import getLogger
import os
import re

from ClusterShell.NodeSet import NodeSet

from exception_utils import CommandFailure
from general_utils import run_task, display_task

SUPPORTED_PROVIDERS = (
    "ofi+sockets", "ofi+tcp", "ofi+tcp;ofi_rxm", "ofi+verbs", "ofi+verbs;ofi_rxm", "ucx+dc_x")


class NetworkDevice():
    """A class to represent the information of a network device."""

    def __init__(self, host, device, ib_device, port, provider, numa):
        """Initialize the network device data object."""
        self.host = host
        self.device = device
        self.ib_device = ib_device
        self.port = port
        self.provider = provider
        self.numa = numa

    def __repr__(self):
        """Overwrite to display formatted devices."""
        return self.__str__()

    def __str__(self):
        """Overwrite to display formatted devices."""
        settings = ["{}={}".format(key, getattr(self, key, None)) for key in self.__dict__]
        return "NetworkDevice({})".format(", ".join(settings))

    def __ne__(self, other):
        """Override the default not-equal implementation."""
        return not self.__eq__(other)

    def __eq__(self, other):
        """Override the default implementation to compare devices."""
        status = isinstance(other, NetworkDevice)
        for key in self.__dict__:
            if not status:
                break
            try:
                status &= getattr(self, key) == getattr(other, key)
            except AttributeError:
                status = False
        return status

    @property
    def domain(self):
        """Get the domain for this network device.

        Returns:
            str: the domain for this network device

        """
        if "ucx" in self.provider:
            return ":".join([self.ib_device, self.port])
        return self.ib_device


def get_active_network_interfaces(hosts, verbose=True):
    """Get all active network interfaces on the hosts.

    Args:
        hosts (NodeSet): hosts on which to find active interfaces
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of interface keys and NodeSet values on which they were found

    """
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    operstate = os.path.join(net_path, "*", "operstate")
    command = " | ".join(
        ["grep -l 'up' {}".format(operstate), "grep -Ev '/(lo|bonding_masters)/'", "sort"])
    task = run_task(hosts, command, verbose=verbose)
    if verbose:
        display_task(task)

    # Populate a dictionary of active interfaces with a NodSet of hosts on which it was found
    active_interfaces = {}
    for output, nodelist in task.iter_buffers():
        output_lines = [line.decode("utf-8") for line in output]
        nodeset = NodeSet.fromlist(nodelist)
        for line in output_lines:
            try:
                interface = line.split("/")[-2]
                if interface not in active_interfaces:
                    active_interfaces[interface] = NodeSet()
                active_interfaces[interface].update(nodeset)
            except IndexError:
                pass

    return active_interfaces


def get_common_interfaces(hosts, verbose=True):
    """Get the active interfaces that exists on all the hosts.

    Args:
        hosts (NodeSet): hosts on which to find active interfaces
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        list: a list of available NetworkDevice objects common to all hosts

    """
    log = getLogger()

    # Find any active network interfaces on the server or client hosts
    interfaces_per_host = get_active_network_interfaces(hosts, verbose)

    # From the active interface dictionary find all the interfaces that are common to all hosts
    log.info("Active network interfaces detected:")
    common_interfaces = []
    for interface, node_set in interfaces_per_host.items():
        log.info("  - %-8s on %s (Common=%s)", interface, node_set, node_set == hosts)
        if node_set == hosts:
            common_interfaces.append(interface)

    return common_interfaces


def get_interface_speeds(hosts, interface, verbose=True):
    """Get the speeds of this network interface on each host.

    Args:
        hosts (NodeSet): hosts on which to detect the interface speed
        interface (str): interface for which to obtain the speed
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of interface speed keys and NodeSet values on which they were detected

    """
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    command = "cat {}".format(os.path.join(net_path, interface, "speed"))
    task = run_task(hosts, command, verbose=verbose)
    if verbose:
        display_task(task)

    # Populate a dictionary of interface speeds with a NodSet of hosts on which it was detected
    interface_speeds = {}
    results = dict(task.iter_retcodes())
    if 0 in results:
        for output, nodelist in task.iter_buffers(results[0]):
            output_lines = [line.decode("utf-8") for line in output]
            nodeset = NodeSet.fromlist(nodelist)
            for line in output_lines:
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
                interface_speeds[speed].update(nodeset)

    return interface_speeds


def get_interface_numa_node(hosts, interface, verbose=True):
    """Get the NUMA node ID of this network interface on each host.

    Args:
        hosts (NodeSet): hosts on which to detect the NUMA node
        interface (str): interface for which to obtain the NUMA node
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of NUMA node ID keys and NodeSet values on which they were detected

    """
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    command = "cat {}".format(os.path.join(net_path, interface, "device", "numa_node"))
    task = run_task(hosts, command, verbose=verbose)
    if verbose:
        display_task(task)

    # Populate a dictionary of numa node IDs with a NodSet of hosts on which it was detected
    numa_nodes = {}
    results = dict(task.iter_retcodes())
    if 0 in results:
        for output, nodelist in task.iter_buffers(results[0]):
            output_lines = [line.decode("utf-8") for line in output]
            nodeset = NodeSet.fromlist(nodelist)
            for line in output_lines:
                try:
                    numa_node = int(line.strip())
                except ValueError:
                    continue
                if numa_node not in numa_nodes:
                    numa_nodes[numa_node] = NodeSet()
                numa_nodes[numa_node].update(nodeset)

    return numa_nodes


def get_interface_ib_name(hosts, interface, verbose=True):
    """Get the InfiniBand name of this network interface on each host.

    Args:
        hosts (NodeSet): hosts on which to detect the InfiniBand name
        interface (str): interface for which to obtain the InfiniBand name
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of InfiniBand name keys and NodeSet values on which they were detected

    """
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    command = "ls -1 {}".format(os.path.join(net_path, interface, "device", "infiniband"))
    task = run_task(hosts, command, verbose=verbose)
    if verbose:
        display_task(task)

    # Populate a dictionary of IB names with a NodSet of hosts on which it was detected
    ib_names = {}
    results = dict(task.iter_retcodes())
    if 0 in results:
        for output, nodelist in task.iter_buffers(results[0]):
            ib_name_list = []
            for line in output:
                match = re.findall(r"([A-Za-z0-9;_+]+)", line.decode("utf-8"))
                if len(match) == 1:
                    ib_name_list.append(match[0])
            if ib_name_list:
                ib_names[",".join(ib_name_list)] = NodeSet.fromlist(nodelist)

    return ib_names


def get_ofi_info(hosts, supported=None, verbose=True):
    """Get the OFI provider information from the specified hosts.

    Args:
        hosts (NodeSet): hosts from which to gather the information
        supported (list, optional): list of supported providers when if provided will limit the
            inclusion to only those providers specified. Defaults to None.
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of interface keys with a dictionary value of a comma-separated string of
            providers key with a NodeSet value where the providers where detected.

    """
    task = run_task(hosts, "fi_info", verbose=verbose)
    if verbose:
        display_task(task)

    # Populate a dictionary of interfaces with a list of provider lists and NodSet of hosts on which
    # the providers were detected.
    providers = {}
    results = dict(task.iter_retcodes())
    if 0 in results:
        for output, nodelist in task.iter_buffers(results[0]):
            output_lines = [line.decode("utf-8").rstrip(os.linesep) for line in output]
            nodeset = NodeSet.fromlist(nodelist)

            # Find all the provider and domain pairings. The fi_info output reports these on
            # separate lines when processing the re matches ensure each domain is preceeded by a
            # provider.
            interface_providers = {}
            data = re.findall(r"(provider|domain):\s+([A-Za-z0-9;_+]+)", "\n".join(output_lines))
            while data:
                provider = list(data.pop(0))
                if provider[0] == "provider" and data[0][0] == "domain":
                    provider.pop(0)
                    domain = list(data.pop(0))
                    domain.pop(0)

                    # A provider and domain must be specified
                    if not provider or not domain:
                        continue

                    # Add 'ofi+' to the provider
                    provider = ["+".join(["ofi", item]) for item in provider]

                    # Only include supported providers if a supported list is provided
                    if supported and provider[0] not in supported:
                        continue

                    if domain[0] not in interface_providers:
                        interface_providers[domain[0]] = set()
                    interface_providers[domain[0]].update(provider)

            for interface, provider_set in interface_providers.items():
                if interface not in providers:
                    providers[interface] = {}
                provider_key = ",".join(list(provider_set))
                if provider_key not in providers[interface]:
                    providers[interface][provider_key] = NodeSet()
                providers[interface][provider_key].update(nodeset)

    return providers


def get_ucx_info(hosts, supported=None, verbose=True):
    """Get the UCX provider information from the specified hosts.

    Args:
        hosts (NodeSet): hosts from which to gather the information
        supported (list, optional): list of supported providers when if provided will limit the
            inclusion to only those providers specified. Defaults to None.
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        dict: a dictionary of interface keys with a dictionary value of a comma-separated string of
            providers key with a NodeSet value where the providers where detected.

    """
    task = run_task(hosts, "ucx_info -d", verbose=verbose)
    if verbose:
        display_task(task)

    # Populate a dictionary of interfaces with a list of provider lists and NodSet of hosts on which
    # the providers were detected.
    providers = {}
    results = dict(task.iter_retcodes())
    if 0 in results:
        for output, nodelist in task.iter_buffers(results[0]):
            output_lines = [line.decode("utf-8").rstrip(os.linesep) for line in output]
            nodeset = NodeSet.fromlist(nodelist)

            # Find all the transport, device, and type pairings. The ucx_info output reports these
            # on separate lines so when processing the re matches ensure each device is preceeded by
            # a provider.
            interface_providers = {}
            data = re.findall(r"(Transport|Device):\s+([A-Za-z0-9;_+]+)", "\n".join(output_lines))
            while data:
                transport = list(data.pop(0))
                if transport[0] == "Transport" and data[0][0] == "Device":
                    transport.pop(0)
                    device = list(data.pop(0))
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
                providers[interface][provider_key].update(nodeset)

    return providers


def get_interface_providers(interface, provider_data):
    """Get the providers supported by this interface.

    Args:
        interface (str): interface for which to obtain the InfiniBand name
        provider_data (dict): output from get_ofi_info() or get_ucx_info()

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


def get_fastest_interface(hosts, verbose=True):
    """Get the fastest active interface common to all hosts.

    Args:
        hosts (NodeSet): hosts on which to find the fastest interfaces
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        str: the fastest active interface common to all hosts specified

    """
    log = getLogger()
    common_interfaces = get_common_interfaces(hosts, verbose)

    # Find the speed of each common active interface in order to be able to choose the fastest
    interface_speeds = {}
    for interface in common_interfaces:
        detected_speeds = get_interface_speeds(hosts, interface, verbose)
        speed_list = []
        for speed, node_set in detected_speeds.items():
            if node_set == hosts:
                # Only include detected homogeneous interface speeds
                speed_list.append(speed)
        if speed_list:
            interface_speeds[interface] = min(speed_list)

    log.info("Active network interface speeds on %s:", hosts)
    available_interfaces = {}
    for interface in sorted(interface_speeds):
        log.info("  - %-8s (speed: %6s)", interface, interface_speeds[interface])

        # Only include the first active interface (as determined by alphabetic sort) for each speed
        if interface_speeds[interface] not in available_interfaces:
            available_interfaces[interface_speeds[interface]] = interface

    log.info("Available interfaces on %s: %s", hosts, available_interfaces)
    try:
        # Select the fastest active interface available by sorting the speed
        interface = available_interfaces[sorted(available_interfaces)[-1]]
    except IndexError:
        interface = None

    log.info("Fastest interface detected on %s: %s", hosts, interface)
    return interface


def get_common_provider(hosts, interface, supported=None, verbose=True):
    """Get the list of providers supported by the interface on every host.

    Args:
        hosts (NodeSet): hosts on which to find the provider information
        interface (str): interface for which to obtain the providers
        supported (list, optional): list of supported providers when if provided will limit the
            inclusion to only those providers specified. Defaults to None.
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        list: a list of providers supported by the interface on every host

    """
    ofi_info = get_ofi_info(hosts, supported, verbose)
    providers = get_interface_providers(interface, ofi_info)

    if not supported or "ucx" in supported:
        ucx_info = get_ucx_info(hosts, supported, verbose)
        for ib_name in get_interface_ib_name(hosts, interface, verbose):
            for add_on in ([], ["1"]):
                device = ":".join([ib_name] + add_on)
                providers.update(get_interface_providers(device, ucx_info))

    # Only include the providers supported by the interface on all of the hosts
    common_providers = set()
    for provider, node_set in providers.items():
        if node_set == hosts:
            common_providers.update(provider.split(","))

    return list(common_providers)


def get_network_information(hosts, supported=None, verbose=True):
    """Get the network device information on the hosts specified.

    Args:
        hosts (NodeSet): hosts on which to find the network information
        supported (list, optional): list of supported providers when if provided will limit the
            inclusion to only those providers specified. Defaults to None.
        verbose (bool, optional): display command details. Defaults to True.

    Returns:
        list: a list of NetworkDevice objects identifying the network devices on each host

    """
    network_devices = []

    ofi_info = get_ofi_info(hosts, supported, verbose)
    ucx_info = get_ucx_info(hosts, supported, verbose)

    interfaces = get_active_network_interfaces(hosts, verbose)
    for host in hosts:
        for interface, node_set in interfaces.items():
            if host in node_set:
                kwargs = {"host": host, "device": interface, "port": 1}
                data_gather = {
                    "ib_device": get_interface_ib_name(node_set, interface, verbose),
                    "provider": get_interface_providers(interface, ofi_info),
                    "numa": get_interface_numa_node(node_set, interface, verbose),
                }
                for ib_name in data_gather["ib_device"]:
                    for add_on in ([], ["1"]):
                        device = ":".join([ib_name] + add_on)
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

                for item in kwargs["provider"].split(","):
                    these_kwargs = kwargs.copy()
                    these_kwargs["provider"] = item
                    network_devices.append(NetworkDevice(**these_kwargs))

    return network_devices


def get_dmg_network_information(dmg_network_scan):
    """Get the network device information from the dmg network scan output.

    Args:
        dmg_network_scan (dict): the dmg network scan json command output

    Raises:
        CommandFailure: if there was an error processing the dmg network scan output

    Returns:
        list: a list of NetworkDevice objects identifying the network devices on each host

    """
    network_devices = []

    try:
        for host_fabric in dmg_network_scan["response"]["HostFabrics"].values():
            for host in NodeSet(host_fabric["HostSet"].split(":")[0]):
                for interface in host_fabric["HostFabric"]["Interfaces"]:
                    network_devices.append(
                        NetworkDevice(
                            host, interface["Device"], None, 1, interface["Provider"],
                            interface["NumaNode"])
                    )
    except KeyError as error:
        raise CommandFailure(
            "Error processing dmg network scan json output: {}".format(dmg_network_scan)) from error

    return network_devices
