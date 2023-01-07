"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from functools import partial
import itertools
from operator import is_not
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

        Args:
            other (StorageDevice): the other object to compare

        Returns:
            bool: True if only the other StorageDevice is an Optane device or both StorageDevices
                are Optane devices and this StorageDevice's address is less than the other
                StorageDevice's address.

        """
        if 'optane' in self.device.lower() and 'optane' in other.device.lower():
            return self.address < other.address
        if 'optane' in self.device.lower():
            return False
        return True

    def __gt__(self, other):
        """Determine if this StorageDevice is greater than the other StorageDevice.

        Args:
            other (StorageDevice): the other object to compare

        Returns:
            bool: True if only this StorageDevice is an Optane device or both StorageDevices
                are Optane devices and this StorageDevice's address is greater than the other
                StorageDevice's address.

        """
        if 'optane' in self.device.lower() and 'optane' in other.device.lower():
            return self.address > other.address
        if 'optane' in self.device.lower():
            return True
        return False

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
    def is_nvme(self):
        """Is this device a NVMe drive.

        Returns:
            bool: True if this device a NVMe drive; False otherwise.

        """
        return len(self.address) == 12 and not self.managed_devices

    @property
    def is_vmd(self):
        """Is this device a VMD drive.

        Returns:
            bool: True if this device a VMD drive; False otherwise.

        """
        return len(self.address) == 13 and not self.managed_devices

    @property
    def is_controller(self):
        """Is this device a VMD controller.

        Returns:
            bool: True if this device an VMD controller; False otherwise.

        """
        return bool(self.managed_devices)


def is_nvme(device):
    """Is this device a NVMe drive.

    Args:
        device (StorageDevice): the device to check

    Returns:
        bool: True if this device a NVMe drive; False otherwise.

    """
    return device.is_nvme


def is_vmd(device):
    """Is this device a VMD drive.

    Args:
        device (StorageDevice): the device to check

    Returns:
        bool: True if this device a VMD drive; False otherwise.

    """
    return device.is_vmd


def is_controller(device):
    """Is this device a VMD controller.

    Args:
        device (StorageDevice): the device to check

    Returns:
        bool: True if this device an VMD controller; False otherwise.

    """
    return device.is_controller


class StorageInfo():
    """Information about host storage."""

    TIER_KEYWORDS = ['pmem', 'md_on_ssd']
    TYPE_SEARCH = {
        'NVMe': r"grep -E 'Class:\s+Non-Volatile memory controller' -B 1 -A 2",
        'VMD': r"grep -E 'Device:\s+Volume Management Device NVMe RAID Controller' -B 2 -A 1",
    }

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
    def nvme_devices(self):
        """Get the list of detected NVMe devices.

        Returns:
            list: a list of NVMe StorageDevice objects

        """
        return list(filter(is_nvme, self.devices))

    @property
    def vmd_devices(self):
        """Get the list of detected VMD devices.

        Returns:
            list: a list of VMD StorageDevice objects

        """
        return list(filter(is_vmd, self.devices))

    @property
    def controller_devices(self):
        """Get the list of detected VMD controller devices.

        Returns:
            list: a list of VMD controller StorageDevice objects

        """
        return list(filter(is_controller, self.devices))

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
        """Detect any NVMe or VMD disks/controllers that exist on every host.

        Args:
            device_filter (str, optional): device search filter. Defaults to None.

        Raises:
            StorageException: if no homogeneous devices are found or there is an error obtaining the
                device information

        """
        self._log.info('Scanning %s for NVMe/VMD devices', self._hosts)
        self._devices.clear()

        for key in sorted(self.TYPE_SEARCH):
            device_info = self.get_device_information(key, device_filter)
            if key == "VMD" and device_info:
                self._devices.extend(self.get_controller_information(device_info))
            else:
                self._devices.extend(device_info)

        if not self._devices:
            keys = ' & '.join(sorted(self.TYPE_SEARCH))
            self._raise_error(f'Error: Non-homogeneous {keys} PCI addresses.')

        self._log.debug('NVMe devices detected in the scan:')
        for device in self.nvme_devices:
            self._log.debug('  - %s', str(device))
        self._log.debug('VMD devices detected in the scan:')
        for device in self.vmd_devices:
            self._log.debug('  - %s', str(device))
        self._log.debug('VMD controllers detected in the scan:')
        for device in self.controller_devices:
            self._log.debug('  - %s', str(device))

    def get_device_information(self, key, device_filter):
        """Get a list of NVMe or VMD devices that exist on every host.

        Args:
            key (str): disk type: 'NVMe' or 'VMD'
            device_filter (str, optional): device search filter. Defaults to None.

        Returns:
            list: the StorageDevice objects found on every host

        """
        found_devices = {}
        self._log.debug(
            'Detecting %s devices on %s%s',
            key, self._hosts, f" with '{device_filter}' filter" if device_filter else '')

        # Find the NVMe devices that exist on every host in the same NUMA slot
        command_list = [
            'lspci -vmm -D', "grep -E '^(Slot|Class|Device|NUMANode):'", self.TYPE_SEARCH[key]]
        command = ' | '.join(command_list) + ' || :'
        result = run_remote(self._log, self._hosts, command)
        if result.passed:
            # Collect all the devices defined by the following lines in the command output:
            #   Slot:   0000:81:00.0
            #   Class:  Non-Volatile memory controller
            #   Device: NVMe Datacenter SSD [Optane]
            #   NUMANode:       1
            for data in result.output:
                all_output = '\n'.join(data.stdout)
                info = {
                    'slots': re.findall(r'Slot:\s+([0-9a-fA-F:.]+)', all_output),
                    'class': re.findall(r'Class:\s+(.*)', all_output),
                    'device': re.findall(r'Device:\s+(.*)', all_output),
                    'numa': re.findall(r'NUMANode:\s+(\d)', all_output),
                }
                for index, address in enumerate(info['slots']):
                    try:
                        device = StorageDevice(
                            address, info['class'][index], info['device'][index],
                            info['numa'][index])
                    except IndexError:
                        self._log.error(
                            '  error creating a StorageDevice object for %s with index %s of the '
                            'following lists:', address, index)
                        self._log.error('    - slots:  %s', info['slots'])
                        self._log.error('    - class:  %s', info['class'])
                        self._log.error('    - device: %s', info['device'])
                        self._log.error('    - numa:   %s', info['numa'])
                        continue

                    # Ignore backing NVMe bound to the kernel driver, e.g., 10005:05:00.0
                    if len(device.address.split(":")[0]) > 4:
                        self._log.debug("  excluding device based on address length: %s", device)
                        continue

                    # Ignore any devices that do not match a filter if specified
                    if device_filter and device_filter.startswith("-"):
                        if re.findall(device_filter[1:], device.description):
                            self._log.debug(
                                "  excluding device matching '%s': %s", device_filter[1:], device)
                            continue
                    elif device_filter and not re.findall(device_filter, device.description):
                        self._log.debug(
                            "  excluding device not matching '%s': %s", device_filter, device)
                        continue

                    # Keep track of which devices were found on which hosts
                    if device not in found_devices:
                        found_devices[device] = NodeSet()
                    found_devices[device].update(data.hosts)

            # Remove any non-homogeneous devices
            for device in list(found_devices):
                if found_devices[device] != self._hosts:
                    self._log.debug(
                        "  device '%s' not found on all hosts, only %s",
                        str(device), found_devices[device])
                    found_devices.pop(device)

        if found_devices:
            self._log.debug('%s devices found on all hosts:', key)
            for device in list(found_devices):
                self._log.debug('  - %s', str(device))
        else:
            self._log.debug('No %s devices found on all hosts', key)

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
        """Get the mapping of each VMD disk to its VMD controller.

        Raises:
            StorageException: if there is an error creating a mapping of disks to controllers

        Returns:
            dict: controller address keys with disk address values

        """
        controllers = {}
        self._log.debug("Determining the controllers for each VMD disk")
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
            for data in result.output:
                for controller, disk in re.findall(regex, '\n'.join(data.stdout)):
                    if controller not in controller_info:
                        controller_info[controller] = {'disks': [], 'count': 0, 'hosts': NodeSet()}
                    if disk not in controller_info[controller]['disks']:
                        controller_info[controller]['disks'].append(disk)
                        controller_info[controller]['count'] += 1
                        controller_info[controller]['hosts'].update(data.hosts)

            # Remove any non-homogeneous devices
            for controller, info in controller_info.items():
                if info['hosts'] != self._hosts:
                    self._log.debug(
                        '  - controller %s not found on all hosts: %s', controller, info)
                elif info['count'] == 0:
                    self._log.debug('  - no disks found for controller %s: %s', controller, info)
                else:
                    controllers[controller] = info['count']

        # Verify each server host has the same NVMe devices behind the same VMD addresses.
        if not controllers:
            self._raise_error("Error: Non-homogeneous NVMe device behind VMD addresses.")

        self._log.debug('Controller/disk mapping')
        for controller, disk_count in controllers.values():
            self._log.debug('  %s: %s disk(s)', controller, disk_count)

        return controllers

    def write_storage_yaml(self, yaml_file, engines, tier_type, scm_size=100):
        """Generate a storage test yaml sub-section.

        Args:
            yaml_file (str): file in which to write the storage yaml entry
            engines (int): number of engines
            tier_type (str): storage type to define; 'pmem' or 'md_on_ssd'
            scm_size (int, optional): scm_size to use with ram storage tiers. Defaults to 100.

        Raises:
            StorageException: if an invalid storage type was specified

        """
        tiers = 1
        self._log.info(
            'Generating a %s storage yaml for %s engines: %s', tier_type, engines, yaml_file)

        if tier_type not in self.TIER_KEYWORDS:
            self._raise_error(f'Error: Invalid storage type \'{tier_type}\'')

        if self.vmd_devices or self.nvme_devices:
            # Sort the detected devices and place then in lists by NUMA node
            numa_devices = {}
            for device in sorted(self.vmd_devices + self.nvme_devices):
                if device.numa_node not in numa_devices:
                    numa_devices[device.numa_node] = []
                numa_devices[device.numa_node].append(device.address)
            self._log.debug('numa_devices:   %s', numa_devices)

            # Interleave the devices for bdev_list distribution.
            if engines > 1:
                # This will also even out any uneven NUMA distribution using the shortest list
                interleaved = list(itertools.chain(*zip(*numa_devices.values())))
            else:
                # Include all devices with uneven NUMA distribution
                interleaved = list(
                    filter(
                        partial(is_not, None),
                        itertools.chain(*itertools.zip_longest(*numa_devices.values()))))
            self._log.debug('interleaved:    %s', interleaved)

            # Break the interleaved (prioritized) disk list into groups of engine size
            device_sets = [interleaved[x:x + engines] for x in range(0, len(interleaved), engines)]
            self._log.debug('device_sets:    %s', device_sets)

            # Tier number device placement order
            tier_placement_priority = [1, 2, 3, 3, 2]

            # Get the tier number device placement for the available number of devices
            tier_placement = []
            while len(tier_placement) < len(device_sets):
                tier_placement.extend(tier_placement_priority)
            tier_placement = sorted(tier_placement[:len(device_sets)])
            self._log.debug('tier_placement: %s', tier_placement)
            tiers += max(tier_placement)

            bdev_list = {}
            for device_set in device_sets:
                tier = tier_placement.pop(0)
                for engine, device in enumerate(device_set):
                    if engine not in bdev_list:
                        bdev_list[engine] = {}
                    if tier not in bdev_list[engine]:
                        bdev_list[engine][tier] = []
                    bdev_list[engine][tier].append(device)
            self._log.debug('bdev_list:      %s', bdev_list)

        lines = ['server_config:', '  engines:']
        for engine in range(engines):
            lines.append(f'    {str(engine)}:')
            lines.append('      storage:')
            for tier in range(tiers):
                lines.append(f'        {str(tier)}:')
                if tier == 0 and tier_type == self.TIER_KEYWORDS[0]:
                    lines.append('          class: dcpm')
                    lines.append(f'          scm_list: ["/dev/pmem{engine}"]')
                    lines.append(f'          scm_mount: /mnt/daos{engine}')
                elif tier == 0:
                    lines.append('          class: ram')
                    lines.append('          scm_list: None')
                    lines.append(f'          scm_mount: /mnt/daos{engine}')
                    lines.append(f'          scm_size: {scm_size}')
                else:
                    lines.append('          class: nvme')
                    lines.append(f'          bdev_list: [{",".join(bdev_list[engine][tier])}]')

        try:
            with open(yaml_file, "w", encoding="utf-8") as config_handle:
                config_handle.writelines(lines)
        except IOError as error:
            self._raise_error(f"Error writing avocado config file {yaml_file}", error)
