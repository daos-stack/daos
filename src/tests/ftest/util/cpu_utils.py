"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import json
import re

from ClusterShell.NodeSet import NodeSet
from general_utils import get_host_data

DATA_ERROR = "[ERROR]"


class CpuException(Exception):
    """Exception for the CpuInfo class."""


class CpuArchitecture():
    # pylint: disable=unnecessary-lambda,too-many-public-methods
    """Information about a CPU architecture."""

    _lscpu_converter = {
        'Architecture:': ('_arch', lambda x: x),
        'CPU op-mode(s):': ('_op_modes', lambda x: [s.strip() for s in x.split(',')]),
        'Byte Order:': ('_byte_order', lambda x: 'little' if x == 'Little Endian' else 'big'),
        'CPU(s):': ('_quantity', lambda x: int(x)),
        'On-line CPU(s) list:': ('_online', lambda x: [int(i) for i in NodeSet('[' + x + ']')]),
        'Thread(s) per core:': ('_threads_core', lambda x: int(x)),
        'Core(s) per socket:': ('_cores_socket', lambda x: int(x)),
        'Socket(s):': ('_sockets', lambda x: int(x)),
        'NUMA node(s):': ('_numas', lambda x: int(x)),
        'Vendor ID:': ('_vendor', lambda x: x),
        'CPU family:': ('_family', lambda x: int(x)),
        'Model:': ('_model', lambda x: int(x)),
        'Model name:': ('_model_name', lambda x: x),
        'Stepping:': ('_stepping', lambda x: int(x)),
        'CPU MHz:': ('_freq', lambda x: float(x)),
        'CPU min MHz:': ('_freq_min', lambda x: float(x)),
        'CPU max MHz:': ('_freq_max', lambda x: float(x)),
        'BogoMIPS:': ('_bogo', lambda x: float(x)),
        'Virtualization:': ('_virt', lambda x: x),
        'L1d cache:': ('_cache_l1d', lambda x: x),
        'L1i cache:': ('_cache_l1i', lambda x: x),
        'L2 cache:': ('_cache_l2', lambda x: x),
        'L3 cache:': ('_cache_l3', lambda x: x),
        'Flags:': ('_flags', lambda x: list(x.split(' ')))
    }
    _lscpu_numas_re = re.compile(r'NUMA node(\d+) CPU\(s\):', re.ASCII)

    def __init__(self, logger, data):
        """Initialize a StorageDevice object.

        Args:
            logger (logger): logger for the messages produced by this class
            data (str): json output of the 'lscpu' command
        """
        self._log = logger
        cpu_arch = json.loads(data)
        self._numa_cpus = {}
        for it in cpu_arch['lscpu']:
            field = it['field']

            if field in CpuArchitecture._lscpu_converter:
                key, func = CpuArchitecture._lscpu_converter[field]
                setattr(self, key, func(it['data']))
                continue

            numa_match = CpuArchitecture._lscpu_numas_re.fullmatch(field)
            if numa_match:
                numa_idx = int(numa_match.group(1))
                self._numa_cpus[numa_idx] = [int(i) for i in NodeSet('[' + it['data'] + ']')]

    def __str__(self):
        """Convert this CpuArchitecture into a string.

        Returns:
            str: the string version of the parameter's value

        """
        msg = "CPU Architecture:\n"
        for key, it in CpuArchitecture._lscpu_converter.items():
            msg += f"\t- {key} "
            attr_name = it[0]
            if hasattr(self, attr_name):
                val = str(getattr(self, attr_name))
                if attr_name == '_online':
                    val = str(NodeSet(val))
                msg += f"{val}\n"
            else:
                msg += "Undefined\n"
        for numa_idx in sorted(self._numa_cpus):
            msg += f"\t- NUMA node{numa_idx}: {NodeSet(str(self._numa_cpus[numa_idx]))}\n"

        return msg

    def __repr__(self):
        """Convert this CpuArchitecture into a string representation.

        Returns:
            str: raw string representation of the parameter's value

        """
        msg = r'{"lscpu":['

        first_it = True
        for key, it in CpuArchitecture._lscpu_converter.items():
            attr_name = it[0]
            if not hasattr(self, attr_name):
                continue
            if first_it:
                first_it = False
            else:
                msg += ','
            msg += '{' + f'"field":"{key}","data":"'
            val = getattr(self, attr_name)
            if attr_name == '_online':
                val = str(NodeSet(str(val)))[1:-1]
            elif attr_name == '_flags':
                val = ' '.join(flag for flag in val)
            elif attr_name == '_op_modes':
                val = ', '.join(mode for mode in val)
            elif attr_name == '_byte_order':
                val = 'Little Endian' if val == 'little' else 'Big Endian'
            else:
                val = str(val)
            msg += f'{val}"' + '}'

        for numa_idx in sorted(self._numa_cpus):
            if first_it:
                first_it = False
            else:
                msg += ','
            msg += '{' + f'"field":"NUMA node{numa_idx} CPU(s):","data":"'
            msg += str(NodeSet(str(self._numa_cpus[numa_idx])))[1:-1] + '"}'

        msg += ']}'

        return msg

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
            raise CpuException(message) from error
        raise CpuException(message)

    def _getattr(self, name):
        """Return the value of the named attribute of object.

        Args:
            name (str): name of the attribute to retrieve

        Returns:
            object: value of the attributes

        """
        if not hasattr(self, name):
            self._raise_error(f"ERROR: the CPU property '{name}' is undefined.")
        return getattr(self, name)

    @property
    def arch(self):
        """Get the CPU architecture name.

        Returns:
            str: the CPU architecture name (e.g. 'x86_64')

        """
        return self._getattr('_arch')

    @property
    def op_modes(self):
        """Get the CPU operations mode(s)

        Returns:
            list: a list of supported CPU operation mode

        """
        return self._getattr('_op_modes')

    @property
    def byte_order(self):
        """Get the endianness of the CPU architecture.

        Returns:
            str: the name of of the endianness ('little' or 'big')

        """
        return self._getattr('_byte_order')

    @property
    def quantity(self):
        """Get the quantity of CPUs.

        Returns:
            int: the quantity of CPUs

        """
        return self._getattr('_quantity')

    @property
    def online(self):
        """Get the list of online CPUs.

        Returns:
            list: a list of online CPUs

        """
        return self._getattr('_online')

    @property
    def threads_core(self):
        """Get the quantity of thread per core.

        Returns:
            int: the quantity of thread per core

        """
        return self._getattr('_threads_core')

    @property
    def cores_socket(self):
        """Get the quantity of core per socket.

        Returns:
            int: the quantity of core per socket

        """
        return self._getattr('_cores_socket')

    @property
    def sockets(self):
        """Get the quantity of socket.

        Returns:
            int: the quantity of socket

        """
        return self._getattr('_sockets')

    @property
    def numas(self):
        """Get the quantity of numa node.

        Returns:
            int: the quantity of numa node

        """
        return self._getattr('_numas')

    @property
    def vendor(self):
        """Get the vendor name identifier.

        Returns:
            str: the vendor name identifier

        """
        return self._getattr('_vendor')

    @property
    def family(self):
        """Get the CPU family identifier.

        Returns:
            int: the CPU family identifier

        """
        return self._getattr('_family')

    @property
    def model(self):
        """Get the CPU model identifier.

        Returns:
            int: the CPU model identifier

        """
        return self._getattr('_model')

    @property
    def model_name(self):
        """Get the CPU model name.

        Returns:
            str: the CPU model name

        """
        return self._getattr('_model_name')

    @property
    def stepping(self):
        """Get the stepping lithography level.

        Returns:
            int: the stepping lithography level

        """
        return self._getattr('_stepping')

    @property
    def freq(self):
        """Get the CPU frequency in MHz.

        Returns:
            float: the CPU frequency in MHz

        """
        return self._getattr('_freq')

    @property
    def freq_min(self):
        """Get the minimal CPU frequency in MHz.

        Returns:
            float: the minimal CPU frequency in MHz

        """
        return self._getattr('_freq_min')

    @property
    def freq_max(self):
        """Get the maximal CPU frequency in MHz.

        Returns:
            float: the maximal CPU frequency in MHz

        """
        return self._getattr('_freq_max')

    @property
    def bogo(self):
        """Get the BogoMips performance measure.

        Returns:
            float: the BogoMips performance measure

        """
        return self._getattr('_bogo')

    @property
    def virt(self):
        """Get the name of the virtualization supported technology.

        Returns:
            str: the name of the virtualization supported technology

        """
        return self._getattr('_virt')

    @property
    def cache_l1d(self):
        """Get the size of the L1 data cache.

        Returns:
            str: the size of the L1 data cache

        """
        return self._getattr('_cache_l1d')

    @property
    def cache_l1i(self):
        """Get the size of the L1 instruction cache.

        Returns:
            str: the size of the L1 instruction cache

        """
        return self._getattr('_cache_l1i')

    @property
    def cache_l2(self):
        """Get the size of the L2 cache.

        Returns:
            str: the size of the L2 cache.

        """
        return self._getattr('_cache_l2')

    @property
    def cache_l3(self):
        """Get the size of the L3 cache.

        Returns:
            str: the size of the L3 cache

        """
        return self._getattr('_cache_l3')

    @property
    def flags(self):
        """Get the list of supported features.

        Returns:
            list: a list of supported features

        """
        return self._getattr('_flags')

    def get_numa_cpus(self, numa_id):
        """Get the list of cpus for a given numa node.

        Args:
            numa_id (int): numa identifier

        Returns:
            list: a list of cpu index

        """
        if numa_id not in self._numa_cpus:
            self._raise_error("Error: Invalid NUMA identifier")
        return self._numa_cpus[numa_id]


class CpuInfo():
    """Information about CPU architecture"""

    def __init__(self, logger, hosts, timeout=60):
        """Initialize a CpuInfo object.

        Args:
            logger (logger): logger for the messages produced by this class
            hosts (NodeSet): set of hosts from which to obtain the CPU architecture information
            timeout (int, optional): timeout used when obtaining host data.
                Defaults to 60 seconds.
        """
        self._log = logger
        self._hosts = hosts.copy()
        self._timeout = timeout
        self._architectures = {}

    def scan(self):
        """Detect the cpu architecture on every host."""
        cmd = 'lscpu --json'
        text = "CPU"
        error = "No CPUs detected"
        host_data = get_host_data(self._hosts, cmd, text, error, self._timeout)

        for it in host_data:
            data = it['data']
            if data == DATA_ERROR:
                self._log.error(f"Error issuing command '{cmd}' on hosts {it['hosts']}")

            key = str(it["hosts"])
            self._architectures[key] = CpuArchitecture(self._log, data)

    def get_architectures(self, hosts=None):
        """Get the cpu architectures of a given set of hosts.

        Args:
            hosts (NodeSet): target hosts
                Defaults to None.

        Returns:
            list: list of couple containing dictionary of cpu architecture for NodeSet of hosts

        """
        hosts_arch = []
        for nodes_str, arch in self._architectures.items():
            nodes = NodeSet(nodes_str)
            if hosts is not None:
                nodes = hosts.intersection(nodes)
                if len(nodes) == 0:
                    continue
            hosts_arch.append((nodes, arch))
        return hosts_arch
