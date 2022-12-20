"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
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

    def __init__(self, address, storage_class, device, numa_node, storage_type):
        """Initialize a StorageDevice object.

        Args:
            address (_type_): _description_
            storage_class (_type_): _description_
            device (_type_): _description_
            numa_node (_type_): _description_
            storage_type (str):
        """
        self.address = address
        self.storage_class = storage_class
        self.device = device
        self.numa_node = numa_node
        self.storage_type = storage_type

    def __str__(self):
        """Convert this StorageDevice into a string.

        Returns:
            str: the string version of the parameter's value

        """
        return ', '.join([str(self.address), self.description, str(self.numa_node)])

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

    def scan(self, device_filter=None, match_mode=None):
        """_summary_.

        Args:
            device_filter (str, optional): device search filter. Defaults to None.
            match_mode (str, optional): disk type to match: 'NVMe' or 'VMD'. Defaults to None which
                will match all types.

        Raises:
            LaunchException: _description_

        """
        self._log.debug('Scanning %s for NVMe/VMD devices', self._hosts)
        self._devices.clear()

        all_devices = {}
        for key in sorted(self.TYPE_SEARCH):
            if not match_mode or key == match_mode:
                all_devices[key] = self.get_disk_information(key, device_filter)
                if key == "VMD":
                    self.get_disk_controllers()

        if not all_devices:
            keys = ' & '.join(sorted(self.TYPE_SEARCH))
            raise StorageException(f'Error: Non-homogeneous {keys} PCI addresses.')

        for device in all_devices:
            self._devices.append(device)

    def get_disk_information(self, key, device_filter):
        """Get a list of NVMe or VMD devices that exist on every host.

        Args:
            key (str): disk type: 'NVMe' or 'VMD'
            device_filter (str, optional): device search filter. Defaults to None.

        Returns:
            list: the StorageDevice objects found on every host

        """
        found_devices = {}
        self._log.debug("Detecting %s devices on %s", key, self._hosts)

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
                    'slots': re.findall(r'Slot:\s+([0-9a-fA-F:]+)', all_output),
                    'class': re.findall(r'Class:\s+(.*)', all_output),
                    'device': re.findall(r'Device:\s+(.*)', all_output),
                    'numa': re.findall(r'NUMANode:\s+(\d)', all_output),
                }
                for index, address in enumerate(info['slots']):
                    try:
                        device = StorageDevice(
                            address, info['class'][index], info['device'][index],
                            info['numa'][index], key)
                    except IndexError:
                        self._log.error(
                            '  error creating a StorageDevice object for %s with index %s of the '
                            'following lists:', address, index)
                        self._log.error('    - slots:  %s', info['slots'])
                        self._log.error('    - class:  %s', info['class'])
                        self._log.error('    - device: %s', info['device'])
                        self._log.error('    - numa:   %s', info['numa'])
                        continue

                    if device_filter and not re.findall(device_filter, device.description):
                        self._log.debug(
                            "  excluding device not matching '%s': %s", device_filter, device)
                        continue

                    if device not in found_devices:
                        found_devices[device] = NodeSet()
                    found_devices[device].update(data.hosts)

            # Remove any non-homogeneous devices
            for device in list(found_devices):
                if found_devices[device] != self._hosts:
                    self._log.debug(
                        "  device '%s' not found on all hosts: %s", key, found_devices[device])
                    found_devices.pop(device)

        return list(found_devices.keys())

    def get_disk_controllers(self):
        """Get the controller for each VMD disk.

        Raises:
            StorageException: if there is an error creating a mapping of disks to controllers

        Returns:
            dict: disk address keys with controller address values

        """
        controllers = {}
        self._log.debug("Determining the controllers for each VMD disk")
        command_list = ["ls -l /sys/block/", "grep nvme"]
        command = " | ".join(command_list) + " || :"
        result = run_remote(self._log, self._hosts, command)

        # Verify the command was successful on each server host
        if not result.passed:
            raise StorageException(f"Error issuing command '{command}'")

        # Map VMD addresses to the disks they control
        controller_mapping = {}
        regex = (r'->\s+\.\./devices/pci[0-9a-f:]+/([0-9a-f:\.]+)/'
                 r'pci[0-9a-f:]+/[0-9a-f:\.]+/([0-9a-f:\.]+)')
        if result.passed:
            for data in result.output:
                for controller, disk in re.findall(regex, '\n'.join(data.stdout)):
                    if disk not in controller_mapping:
                        controller_mapping[disk] = {}
                    if controller not in controller_mapping[disk]:
                        controller_mapping[disk][controller] = NodeSet()
                    controller_mapping[disk][controller].update(data.hosts)

            # Remove any non-homogeneous devices
            for disk in list(controller_mapping):
                for controller in list(disk):
                    if controller_mapping[disk][controller] != self._hosts:
                        self._log.debug(
                            '  - disk %s managed by controller %s not found on all hosts: %s',
                            disk, controller, controller_mapping[disk][controller])
                    else:
                        self._log.debug('  - disk %s managed by controller %s', disk, controller)
                        controllers[disk] = controller

        # Verify each server host has the same NVMe devices behind the same VMD addresses.
        if not controllers:
            raise StorageException("Error: Non-homogeneous NVMe device behind VMD addresses.")

        return controllers

    def set_storage_yaml(self, yaml_file, engines, tiers, tier_type, scm_size=100):
        """Generate a storage test yaml sub-section.

        Args:
            yaml_file (str): file in which to write the storage yaml entry
            engines (int): number of engines
            tiers (int): number of storage tiers per engine
            tier_type (str): storage type to define; 'pmem' or 'md_on_ssd'
            scm_size (int, optional): scm_size to use with ram storage tiers. Defaults to 100.

        Raises:
            StorageException: if an invalid storage type was specified

        """
        if tier_type not in self.TIER_KEYWORDS:
            raise StorageException(f'Error: Invalid storage type \'{tier_type}\'')

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
                    lines.append('          bdev_list: []')

        try:
            with open(yaml_file, "w", encoding="utf-8") as config_handle:
                config_handle.writelines(lines)
        except IOError as error:
            raise StorageException(f"Error writing avocado config file {yaml_file}") from error
