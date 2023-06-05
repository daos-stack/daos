"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from collections import OrderedDict
from functools import partial
import itertools
from operator import is_not
import os
import re

from ClusterShell.NodeSet import NodeSet

from run_utils import run_remote


def find_pci_address(value):
    """Find PCI addresses in the specified string.

    Args:
        value (str): string to search for PCI addresses

    Returns:
        list: a list of all the PCI addresses found in the string

    """
    digit = '0-9a-fA-F'
    pattern = rf'[{digit}]{{4,5}}:[{digit}]{{2}}:[{digit}]{{2}}\.[{digit}]'
    return re.findall(pattern, str(value))


def get_tier_roles(tier, total_tiers):
    """Get the roles for the specified bdev storage tier.

    Args:
        tier (int): the storage bdev tier number
        total_tiers (int): total number of storage tiers

    Raises:
        ValueError: if the specified tier is not a bdev tier

    Returns:
        list: the roles for specified bdev tier

    """
    if tier == 0:
        raise ValueError(f'Inappropriate bdev tier number: {tier}')
    if tier == 1 and total_tiers == 2:
        # A single bdev tier is assigned all roles
        return ['wal', 'data', 'meta']
    if tier == 1:
        # The first of multiple bdev tiers in is assigned the wal role
        return ['wal']
    if tier == 2 and total_tiers == 3:
        # The second of two bdev tiers in is assigned the data and meta role
        return ['data', 'meta']
    if tier == 2:
        # The second of three or more bdev tiers in is assigned the meta
        return ['meta']
    # Any additional bdev tiers are assigned the data role
    return ['data']


class StorageException(Exception):
    """Exception for the StorageInfo class."""


class StorageDevice():
    """Information about a storage device."""

    def __init__(self, address, storage_class, device, numa_node):
        """Initialize a StorageDevice object.

        Args:
            address (str): the address of the device
            storage_class (str): the device class description
            device (str): the device description
            numa_node (str): the NUMA node number
        """
        self.address = address
        self.storage_class = storage_class
        self.device = device
        self.numa_node = numa_node
        self.managed_devices = 0

    def __str__(self):
        """Convert this StorageDevice into a string.

        Returns:
            str: the string version of the parameter's value

        """
        return ' - '.join([str(self.address), self.description, str(self.numa_node)])

    def __repr__(self):
        """Convert this StorageDevice into a string representation.

        Returns:
            str: raw string representation of the parameter's value

        """
        return self.__str__()

    def __eq__(self, other):
        """Determine if this StorageDevice is equal to another StorageDevice.

        Args:
            other (StorageDevice): the other object to compare

        Returns:
            bool: whether or not this StorageDevice is equal to another StorageDevice

        """
        return str(self) == str(other)

    def __lt__(self, other):
        """Determine if this StorageDevice is less than the other StorageDevice.

        Used to sort devices by their addresses, but with more performant devices listed first.

        Args:
            other (StorageDevice): the other object to compare

        Returns:
            bool: True if only this StorageDevice is more performant, False if the other
                StorageDevice is more performant, or True if this StorageDevice address is less
                than the other StorageDevice.

        """
        if 'optane' in self.device.lower() and 'optane' not in other.device.lower():
            # This device is more performant than the other device
            return True
        if 'optane' not in self.device.lower() and 'optane' in other.device.lower():
            # The other device is more performant than this device
            return False
        return self.address < other.address

    def __gt__(self, other):
        """Determine if this StorageDevice is greater than the other StorageDevice.

        Used to sort devices by their addresses, but with more performant devices listed first.

        Args:
            other (StorageDevice): the other object to compare

        Returns:
            bool: True if only the other StorageDevice is more performant, False if this
                StorageDevice is more performant, or True if this StorageDevice address is greater
                than the other StorageDevice.

        """
        return not self.__lt__(other)

    def __hash__(self):
        """Get the hash value of this StorageDevice object.

        Returns:
            int: hash value of this StorageDevice object

        """
        return hash(str(self))

    @property
    def description(self):
        """Get the StorageDevice description.

        Returns:
            str: the description of this StorageDevice

        """
        return ': '.join([self.storage_class, self.device])

    @property
    def is_backing(self):
        """Is this device a backing device behind a VMD controller.

        Returns:
            bool: True if this device is a backing device; False otherwise.

        """
        return self.is_disk and len(self.address.split(':')[0]) > 4

    @property
    def is_controller(self):
        """Is this device a VMD controller.

        Returns:
            bool: True if this device an VMD controller; False otherwise.

        """
        return bool(self.managed_devices)

    @property
    def is_disk(self):
        """Is this device a disk.

        Returns:
            bool: True if this device a disk; False otherwise.

        """
        return not self.is_controller and not self.is_pmem

    @property
    def is_pmem(self):
        """Is this a PMEM device.

        Returns:
            bool: True if this a PMEM device; False otherwise.

        """
        return self.storage_class == "PMEM"


class StorageInfo():
    """Information about host storage."""

    TIER_0_TYPES = ('pmem', 'ram')
    TIER_NVME_PRIORITY = (1, 2, 3, 3, 2)
    TYPE_SEARCH = OrderedDict(
        [
            ('PMEM', [
                'ndctl list -c -v',
                "grep -v 'uuid'"]),
            ('NVMe', [
                'lspci -vmm -D',
                "grep -E '^(Slot|Class|Device|NUMANode):'",
                r"grep -E 'Class:\s+Non-Volatile memory controller' -B 1 -A 2"]),
            ('VMD', [
                'lspci -vmm -D',
                "grep -E '^(Slot|Class|Device|NUMANode):'",
                r"grep -E 'Device:\s+Volume Management Device NVMe RAID Controller' -B 2 -A 1"]),
        ]
    )

    def __init__(self, logger, hosts):
        """Initialize a StorageInfo object.

        Args:
            logger (logger): logger for the messages produced by this class
            hosts (NodeSet): set of hosts from which to obtain the storage device information
        """
        self._log = logger
        self._hosts = hosts.copy()
        self._devices = []

    @property
    def devices(self):
        """Get the devices found on the hosts.

        Returns:
            list: a list of StorageDevice objects

        """
        return self._devices

    @property
    def pmem_devices(self):
        """Get the list of detected PMEM devices.

        Returns:
            list: a list of PMEM StorageDevice objects

        """
        return list(filter(StorageDevice.is_pmem.fget, self.devices))

    @property
    def disk_devices(self):
        """Get the list of detected disk devices.

        Returns:
            list: a list of disk StorageDevice objects

        """
        return list(filter(StorageDevice.is_disk.fget, self.devices))

    @property
    def controller_devices(self):
        """Get the list of detected VMD controller devices.

        Returns:
            list: a list of VMD controller StorageDevice objects

        """
        return list(filter(StorageDevice.is_controller.fget, self.devices))

    def device_dict(self):
        """Get the scanned devices as a dictionary.

        Returns:
            dict: device type keys with device information string values

        """
        data = {}
        for key, name in {'PMEM': 'pmem', 'NVMe': 'disk', 'VMD': 'controller'}.items():
            devices = getattr(self, f'{name}_devices')
            if devices:
                data[key] = [str(item) for item in devices]
        return data

    def _raise_error(self, message, error=None):
        """Raise and log the error message.

        Args:
            message (str): error description
            error (optional, Exception): exception from which to raise. Defaults to None.

        Raises:
            StorageException: with the provided error description

        """
        self._log.error(message)
        if error:
            raise StorageException(message) from error
        raise StorageException(message)

    def scan(self, device_filter=None):
        """Detect any PMEM, NVMe, or VMD disks/controllers that exist on every host.

        Args:
            device_filter (str, optional): device search filter. Defaults to None.

        Raises:
            StorageException: if no homogeneous devices are found or there is an error obtaining the
                device information

        """
        self._devices.clear()
        self._log.info('Scanning %s for PMEM/NVMe/VMD devices', self._hosts)
        for key in self.TYPE_SEARCH:
            device_info = self.get_device_information(key, device_filter)
            if key == "VMD" and device_info:
                self._devices.extend(self.get_controller_information(device_info))
            else:
                self._devices.extend(device_info)

    def get_device_information(self, key, device_filter):
        """Get a list of PMEM, NVMe, or VMD devices that exist on every host.

        Args:
            key (str): disk type: 'PMEM', 'NVMe', or 'VMD'
            device_filter (str, optional): device search filter. Defaults to None.

        Returns:
            list: the StorageDevice objects found on every host

        """
        found_devices = {}
        self._log.debug(
            'Detecting %s devices on %s%s',
            key, self._hosts, f" with '{device_filter}' filter" if device_filter else '')

        if key not in self.TYPE_SEARCH:
            self._raise_error(f'Error: Invalid storage type \'{key}\'')

        # Find the NVMe devices that exist on every host in the same NUMA slot
        command = ' | '.join(self.TYPE_SEARCH[key]) + ' || :'
        result = run_remote(self._log, self._hosts, command)
        if result.passed:
            # Collect all the devices defined by the command output
            self._log.debug('Processing device information')
            for data in result.output:
                all_output = '\n'.join(data.stdout)
                if key == 'PMEM':
                    info_key = 'blockdev'
                    info = {
                        'size': re.findall(r'"size":(\d+),', all_output),
                        'blockdev': re.findall(r'"blockdev":"(.*)",', all_output),
                        'map': re.findall(r'"map":"(.*)",', all_output),
                        'numa': re.findall(r'"numa_node":(\d),', all_output),
                    }
                else:
                    info_key = 'slots'
                    info = {
                        'slots': re.findall(r'Slot:\s+([0-9a-fA-F:.]+)', all_output),
                        'class': re.findall(r'Class:\s+(.*)', all_output),
                        'device': re.findall(r'Device:\s+(.*)', all_output),
                        'numa': re.findall(r'NUMANode:\s+(\d)', all_output),
                    }
                for index, item in enumerate(info[info_key]):
                    try:
                        if key == 'PMEM':
                            kwargs = {
                                'address': os.path.join(os.sep, info['map'][index], item),
                                'storage_class': key,
                                'device': info['size'][index],
                                'numa_node': info['numa'][index],
                            }
                        else:
                            kwargs = {
                                'address': item,
                                'storage_class': info['class'][index],
                                'device': info['device'][index],
                                'numa_node': info['numa'][index],
                            }
                        device = StorageDevice(**kwargs)
                    except IndexError:
                        self._log.error(
                            '  error creating a StorageDevice object for %s with index %s: %s',
                            item, index, info)
                        continue

                    # Ignore backing NVMe bound to the kernel driver, e.g., 10005:05:00.0
                    if device.is_backing:
                        self._log.debug("  excluding backing device: %s", device)
                        continue

                    # Ignore any devices that do not match a filter if specified
                    if device_filter and device_filter.startswith("-"):
                        if re.findall(device_filter[1:], device.description):
                            self._log.debug(
                                "  excluding device matching '%s' filter: %s",
                                device_filter[1:], device)
                            continue
                    elif device_filter and not re.findall(device_filter, device.description):
                        self._log.debug(
                            "  excluding device not matching '%s' filter: %s",
                            device_filter, device)
                        continue

                    # Keep track of which devices were found on which hosts
                    if device not in found_devices:
                        found_devices[device] = NodeSet()
                    found_devices[device].update(data.hosts)

            # Remove any non-homogeneous devices
            for device in list(found_devices):
                if found_devices[device] != self._hosts:
                    self._log.debug(
                        "  device '%s' not found on all hosts: %s != %s",
                        str(device), found_devices[device], str(self._hosts))
                    found_devices.pop(device)

        if found_devices:
            self._log.debug('%s devices found on %s', key, str(self._hosts))
            for device in sorted(list(found_devices)):
                self._log.debug('  - %s', str(device))
        else:
            self._log.debug('No %s devices found on %s', key, str(self._hosts))

        return list(found_devices.keys())

    def get_controller_information(self, device_info):
        """Get a list of VMD controllers which have disks to manage.

        Args:
            device_info (list): a list of detected StorageDevice controller objects

        Returns:
            list: the StorageDevice controller objects with managed disks

        """
        controllers = []
        controller_mapping = self.get_controller_mapping()
        for device in device_info:
            if device.address in controller_mapping:
                device.managed_devices = controller_mapping[device.address]
                controllers.append(device)
        return controllers

    def get_controller_mapping(self):
        """Get the mapping of each VMD controller to the number of managed devices.

        Raises:
            StorageException: if there is an error creating a controller mapping

        Returns:
            dict: controller address keys with managed disk count values

        """
        controllers = {}
        self._log.debug("Determining the number of devices behind each VMD controller")
        command_list = ["ls -l /sys/block/", "grep nvme"]
        command = " | ".join(command_list) + " || :"
        result = run_remote(self._log, self._hosts, command)

        # Verify the command was successful on each server host
        if not result.passed:
            self._raise_error(f"Error issuing command '{command}'")

        # Map VMD addresses to the disks they control
        controller_info = {}
        regex = (r'->\s+\.\./devices/pci[0-9a-f:]+/([0-9a-f:\.]+)/'
                 r'pci[0-9a-f:]+/[0-9a-f:\.]+/([0-9a-f:\.]+)')
        if result.passed:
            self._log.debug('Processing controller disk information')
            for data in result.output:
                disks_per_controller = {}
                for controller, _ in re.findall(regex, '\n'.join(data.stdout)):
                    # Keep track of which controllers were found on which hosts
                    if controller not in controller_info:
                        controller_info[controller] = {'hosts': NodeSet(), 'count': set()}
                    controller_info[controller]['hosts'].update(data.hosts)

                    # Increment the total number of disks found for this controller
                    if controller not in disks_per_controller:
                        disks_per_controller[controller] = 0
                    disks_per_controller[controller] += 1

                # Update the number of disks found for each controller on this set of hosts
                for controller, count in disks_per_controller.items():
                    controller_info[controller]['count'].add(count)

            # Remove any non-homogeneous devices
            for controller, info in controller_info.items():
                self._log.debug(
                    '  Controller %s: disks: %s, hosts: %s',
                    controller, info['count'], str(info['hosts']))
                if info['hosts'] != self._hosts:
                    self._log.debug(
                        '    - controller %s not found on all hosts: %s != %s',
                        controller, str(info['hosts']), str(self._hosts))
                elif len(info['count']) != 1:
                    self._log.debug(
                        '    - non-homogeneous disk count found for controller %s: %s',
                        controller, info['count'])
                else:
                    controllers[controller] = list(info['count'])[0]

        # Verify each server host has the same NVMe devices behind the same VMD addresses.
        if not controllers:
            self._raise_error("Error: Non-homogeneous NVMe device behind VMD addresses.")

        self._log.debug('Controllers found with equal disk quantity on %s', str(self._hosts))
        for controller, disk_count in controllers.items():
            self._log.debug('  %s: %s disk(s)', controller, disk_count)

        return controllers

    def write_storage_yaml(self, yaml_file, engines, tier_0_type, scm_size=0,
                           scm_mount='/mnt/daos', max_nvme_tiers=1, control_metadata=None):
        """Generate a storage test yaml sub-section.

        Args:
            yaml_file (str): file in which to write the storage yaml entry
            engines (int): number of engines
            tier_0_type (str): storage tier 0 type: 'pmem' or 'ram'
            scm_size (int, optional): scm_size to use with ram storage tiers. Defaults to 0 (auto).
            scm_mount (str): the base path for the storage tier 0 scm_mount.
            max_nvme_tiers (int): maximum number of nvme storage tiers. Defaults to 1.
            control_metadata (str, optional): directory to store control plane metadata when using
                metadata on SSD. Defaults to None.

        Raises:
            StorageException: if an invalid storage type was specified

        """
        tiers = 1
        self._log.info(
            'Generating a %s storage yaml for %s engines: %s', tier_0_type, engines, yaml_file)

        if tier_0_type not in self.TIER_0_TYPES:
            self._raise_error(f'Error: Invalid storage type \'{tier_0_type}\'')

        pmem_list = {}
        bdev_list = {}

        if tier_0_type == self.TIER_0_TYPES[0] and self.pmem_devices:
            # Sort the detected devices and place then in lists by NUMA node
            numa_devices = self._get_numa_devices(self.pmem_devices)
            self._log.debug('  PMEM numa_devices:   %s', numa_devices)

            # Interleave the devices for bdev_list distribution.
            interleaved = self._get_interleaved(engines, numa_devices)
            self._log.debug('  PMEM interleaved:    %s', interleaved)

            if len(interleaved) >= engines:
                for engine in range(engines):
                    pmem_list[engine] = interleaved.pop(0)
            self._log.debug('  PMEM pmem_list:      %s', pmem_list)

        if self.controller_devices or self.disk_devices:
            # Sort the detected devices and place then in lists by NUMA node
            numa_devices = self._get_numa_devices(self.controller_devices or self.disk_devices)
            self._log.debug('  NVMe/VMD numa_devices:   %s', numa_devices)

            # Interleave the devices for bdev_list distribution.
            interleaved = self._get_interleaved(engines, numa_devices)
            self._log.debug('  NVMe/VMD interleaved:    %s', interleaved)

            # Break the interleaved (prioritized) disk list into groups of engine size
            device_sets = [interleaved[x:x + engines] for x in range(0, len(interleaved), engines)]
            self._log.debug('  NVMe/VMD device_sets:    %s', device_sets)

            # Get the tier number device placement for the available number of devices
            tier_placement = []
            while len(tier_placement) < len(device_sets):
                tier_placement.extend(self.TIER_NVME_PRIORITY[:max_nvme_tiers])
            tier_placement = sorted(tier_placement[:len(device_sets)])
            self._log.debug('  NVMe/VMD tier_placement: %s', tier_placement)
            tiers += max(tier_placement)

            for device_set in device_sets:
                tier = tier_placement.pop(0)
                for engine, device in enumerate(device_set):
                    if engine not in bdev_list:
                        bdev_list[engine] = {}
                    if tier not in bdev_list[engine]:
                        bdev_list[engine][tier] = []
                    bdev_list[engine][tier].append(f'"{device}"')
            self._log.debug('  NVMe/VMD bdev_list:      %s', bdev_list)

        lines = ['server_config:']
        if control_metadata and bdev_list:
            lines.append('  control_metadata:')
            lines.append(f'    path: {control_metadata}')
        lines.append('  engines:')
        for engine in range(engines):
            lines.append(f'    {str(engine)}:')
            lines.append('      storage:')
            for tier in range(tiers):
                lines.append(f'        {str(tier)}:')
                if tier == 0 and pmem_list:
                    lines.append('          class: dcpm')
                    lines.append(f'          scm_list: ["{pmem_list[engine]}"]')
                    lines.append(f'          scm_mount: {scm_mount}{engine}')
                elif tier == 0:
                    lines.append('          class: ram')
                    lines.append(f'          scm_mount: {scm_mount}{engine}')
                    lines.append(f'          scm_size: {scm_size}')
                else:
                    lines.append('          class: nvme')
                    lines.append(f'          bdev_list: [{", ".join(bdev_list[engine][tier])}]')
                    if control_metadata:
                        lines.append(
                            f'          bdev_roles: [{", ".join(get_tier_roles(tier, tiers))}]')

        self._log.debug('  Creating %s', yaml_file)
        for line in lines:
            self._log.debug('    %s', line)
        try:
            with open(yaml_file, "w", encoding="utf-8") as config_handle:
                config_handle.writelines(f'{line}\n' for line in lines)
        except IOError as error:
            self._raise_error(f"Error writing avocado config file {yaml_file}", error)

    @staticmethod
    def _get_numa_devices(devices):
        """Get a dictionary of sorted devices indexed by their NUMA node.

        Args:
            devices (list): list of StorageDevice devices

        Returns:
            dict: dictionary of sorted devices indexed by their NUMA node

        """
        numa_devices = OrderedDict()
        for device in sorted(devices):
            if device.numa_node not in numa_devices:
                numa_devices[device.numa_node] = []
            numa_devices[device.numa_node].append(device.address)
        return numa_devices

    @staticmethod
    def _get_interleaved(engines, numa_devices):
        """Get an interleaved list of NUMA devices.

        Args:
            engines (int): number of engines
            numa_devices (dict): dictionary of sorted devices indexed by their NUMA node

        Returns:
            list: interleaved list of NUMA devices

        """
        if engines > 1:
            # This will also even out any uneven NUMA distribution using the shortest list
            return list(itertools.chain(*zip(*numa_devices.values())))

        # Include all devices with uneven NUMA distribution for single engine configurations
        return list(
            filter(
                partial(is_not, None),
                itertools.chain(*itertools.zip_longest(*numa_devices.values()))))
