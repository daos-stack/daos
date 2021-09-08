#!/usr/bin/python
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import os

from ClusterShell.NodeSet import NodeSet

from command_utils_base import FormattedParameter, CommandWithParameters
from command_utils import YamlCommand, CommandWithSubCommand, CommandFailure
from general_utils import get_display_size, human_to_bytes


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class AutosizeCancel(Exception):
    """Cancel a test due to the inability to autosize a pool parameter."""


class DaosServerCommand(YamlCommand):
    """Defines an object representing the daos_server command."""

    NORMAL_PATTERN = "DAOS I/O Engine.*started"
    FORMAT_PATTERN = "(SCM format required)(?!;)"
    REFORMAT_PATTERN = "Metadata format required"

    DEFAULT_CONFIG_FILE = os.path.join(os.sep, "etc", "daos", "daos_server.yml")

    def __init__(self, path="", yaml_cfg=None, timeout=30):
        """Create a daos_server command object.

        Args:
            path (str): path to location of daos_server binary.
            yaml_cfg (YamlParameters, optional): yaml configuration parameters.
                Defaults to None.
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 30 seconds.
        """
        super().__init__(
            "/run/daos_server/*", "daos_server", path, yaml_cfg, timeout)
        self.pattern = self.NORMAL_PATTERN

        # If specified use the configuration file from the YamlParameters object
        default_yaml_file = None
        if self.yaml is not None and hasattr(self.yaml, "filename"):
            default_yaml_file = self.yaml.filename

        # Command line parameters:
        # -d, --debug        Enable debug output
        # -J, --json-logging Enable JSON logging
        # -o, --config-path= Path to agent configuration file
        self.debug = FormattedParameter("--debug", True)
        self.json_logs = FormattedParameter("--json-logging", False)
        self.config = FormattedParameter("--config={}", default_yaml_file)

        # Additional daos_server command line parameters:
        #     --allow-proxy  Allow proxy configuration via environment
        self.allow_proxy = FormattedParameter("--allow-proxy", False)

        # Used to override the sub_command.value parameter value
        self.sub_command_override = None

        # Include the daos_engine command launched by the daos_server
        # command.
        self._exe_names.append("daos_engine")

    def get_sub_command_class(self):
        # pylint: disable=redefined-variable-type
        """Get the daos_server sub command object based upon the sub-command."""
        if self.sub_command_override is not None:
            # Override the sub_command parameter
            sub_command = self.sub_command_override
        else:
            # Use the sub_command parameter from the test's yaml
            sub_command = self.sub_command.value

        # Available daos_server sub-commands:
        #   network  Perform network device scan based on fabric provider
        #   start    Start daos_server
        #   storage  Perform tasks related to locally-attached storage
        if sub_command == "network":
            self.sub_command_class = self.StartSubCommand()
        elif sub_command == "start":
            self.sub_command_class = self.StartSubCommand()
        elif sub_command == "storage":
            self.sub_command_class = self.StorageSubCommand()
        else:
            self.sub_command_class = None

    def get_params(self, test):
        """Get values for the daos command and its yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)

        # Run daos_server with test variant specific log file names if specified
        self.yaml.update_log_files(
            getattr(test, "control_log"),
            getattr(test, "helper_log"),
            getattr(test, "server_log")
        )

    def update_pattern(self, mode, host_qty):
        """Update the pattern used to determine if the daos_server started.

        Args:
            mode (str): operation mode for the 'daos_server start' command
            host_qty (int): number of hosts issuing 'daos_server start'
        """
        if mode == "format":
            self.pattern = self.FORMAT_PATTERN
        elif mode == "reformat":
            self.pattern = self.REFORMAT_PATTERN
        else:
            self.pattern = self.NORMAL_PATTERN
        self.pattern_count = host_qty * len(self.yaml.engine_params)

    @property
    def using_nvme(self):
        """Is the daos command setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured; False otherwise

        """
        value = False
        if self.yaml is not None and hasattr(self.yaml, "using_nvme"):
            value = self.yaml.using_nvme
        return value

    @property
    def using_dcpm(self):
        """Is the daos command setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured; False otherwise

        """
        value = False
        if self.yaml is not None and hasattr(self.yaml, "using_dcpm"):
            value = self.yaml.using_dcpm
        return value

    @property
    def engine_params(self):
        """Get the configuration parameters for each server engine.

        Returns:
            list: a list of YamlParameters for each server engine

        """
        engine_params = []
        if self.yaml is not None and hasattr(self.yaml, "engine_params"):
            engine_params = self.yaml.engine_params
        return engine_params

    def get_engine_values(self, name):
        """Get the value of the specified attribute name for each engine.

        Args:
            name (str): name of the attribute from which to get the value

        Returns:
            list: a list of the value of each matching configuration attribute
                name per engine

        """
        engine_values = []
        if self.yaml is not None and hasattr(self.yaml, "get_engine_values"):
            engine_values = self.yaml.get_engine_values(name)
        return engine_values

    class NetworkSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server network sub command."""

        def __init__(self):
            """Create a network subcommand object."""
            super().__init__(
                "/run/daos_server/network/*", "network")

        def get_sub_command_class(self):
            """Get the daos_server network sub command object."""
            # Available daos_server network sub-commands:
            #   list  List all known OFI providers that are understood by 'scan'
            #   scan  Scan for network interface devices on local server
            if self.sub_command.value == "scan":
                self.sub_command_class = self.ScanSubCommand()
            else:
                self.sub_command_class = None

        class ScanSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server network scan command."""

            def __init__(self):
                """Create a network scan subcommand object."""
                super().__init__("/run/daos_server/network/scan/*", "scan")

                # daos_server network scan command options:
                #   --provider=     Filter device list to those that support the
                #                   given OFI provider (default is the provider
                #                   specified in daos_server.yml)
                #   --all           Specify 'all' to see all devices on all
                #                   providers.  Overrides --provider
                self.provider = FormattedParameter("--provider={}")
                self.all = FormattedParameter("--all", False)

    class StartSubCommand(CommandWithParameters):
        """Defines an object representing a daos_server start sub command."""

        def __init__(self):
            """Create a start subcommand object."""
            super().__init__("/run/daos_server/start/*", "start")

            # daos_server start command options:
            #   --port=                 Port for the gRPC management interface
            #                           to listen on
            #   --storage=              Storage path
            #   --modules=              List of server modules to load
            #   --targets=              number of targets to use (default use
            #                           all cores)
            #   --xshelpernr=           number of helper XS per VOS target
            #   --firstcore=            index of first core for service thread
            #                           (default: 0)
            #   --group=                Server group name
            #   --socket_dir=           Location for all daos_server and
            #                           daos_engine sockets
            #   --insecure              allow for insecure connections
            #   --recreate-superblocks  recreate missing superblocks rather than
            #                           failing
            self.port = FormattedParameter("--port={}")
            self.storage = FormattedParameter("--storage={}")
            self.modules = FormattedParameter("--modules={}")
            self.targets = FormattedParameter("--targets={}")
            self.xshelpernr = FormattedParameter("--xshelpernr={}")
            self.firstcore = FormattedParameter("--firstcore={}")
            self.group = FormattedParameter("--group={}")
            self.sock_dir = FormattedParameter("--socket_dir={}")
            self.insecure = FormattedParameter("--insecure", False)
            self.recreate = FormattedParameter("--recreate-superblocks", False)

    class StorageSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server storage sub command."""

        def __init__(self):
            """Create a storage subcommand object."""
            super().__init__("/run/daos_server/storage/*", "storage")

        def get_sub_command_class(self):
            """Get the daos_server storage sub command object."""
            # Available sub-commands:
            #   prepare  Prepare SCM and NVMe storage attached to remote servers
            #   scan     Scan SCM and NVMe storage attached to local server
            if self.sub_command.value == "prepare":
                self.sub_command_class = self.PrepareSubCommand()
            else:
                self.sub_command_class = None

        class PrepareSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server storage prepare command."""

            def __init__(self):
                """Create a storage subcommand object."""
                super().__init__(
                    "/run/daos_server/storage/prepare/*", "prepare")

                # daos_server storage prepare command options:
                #   --pci-allowlist=    Whitespace separated list of PCI
                #                       devices (by address) to be unbound from
                #                       Kernel driver and used with SPDK
                #                       (default is all PCI devices).
                #   --hugepages=        Number of hugepages to allocate (in MB)
                #                       for use by SPDK (default 1024)
                #   --target-user=      User that will own hugepage mountpoint
                #                       directory and vfio groups.
                #   --nvme-only         Only prepare NVMe storage.
                #   --scm-only          Only prepare SCM.
                #   --reset             Reset SCM modules to memory mode after
                #                       removing namespaces. Reset SPDK
                #                       returning NVMe device bindings back to
                #                       kernel modules.
                #   --force             Perform format without prompting for
                #                       confirmation
                self.pci_allowlist = FormattedParameter("--pci-allowlist={}")
                self.hugepages = FormattedParameter("--hugepages={}")
                self.target_user = FormattedParameter("--target-user={}")
                self.nvme_only = FormattedParameter("--nvme-only", False)
                self.scm_only = FormattedParameter("--scm-only", False)
                self.reset = FormattedParameter("--reset", False)
                self.force = FormattedParameter("--force", False)


class DaosServerInformation():
    """An object that stores the daos_server storage and network scan data."""

    def __init__(self, dmg):
        """Create a DaosServerInformation object.

        Args:
            dmg (DmgCommand): dmg command configured to communicate with the
                servers
        """
        self.log = getLogger(__name__)
        self.dmg = dmg
        self.storage = {}
        self.network = {}

    def collect_storage_information(self):
        """Collect storage information from the servers.

        Assigns the self.storage dictionary.
        """
        try:
            self.storage = self.dmg.storage_scan()
        except CommandFailure:
            self.storage = {}

    def collect_network_information(self):
        """Collect storage information from the servers.

        Assigns the self.network dictionary.
        """
        try:
            self.network = self.dmg.network_scan()
        except CommandFailure:
            self.network = {}

    def _check_information(self, key, section, retry=True):
        """Check that the information dictionary is populated with data.

        Args:
            key (str): the information section to verify, e.g. "storage" or
                "network"
            section (str): The "response" key/section to verify
            retry (bool): if set attempt to collect the server storage or
                network information and call the method again. Defaults to True.

        Raises:
            ServerFailed: if output from the dmg command is missing or not in
                the expected format

        """
        msg = ""
        entry = getattr(self, key, None)
        if not entry:
            self.log.info("Server storage/network information not collected")
            if retry:
                collect_method = {
                    "storage": self.collect_storage_information,
                    "network": self.collect_network_information}
                if key in collect_method:
                    collect_method[key]()
                self._check_information(key, section, retry=False)
        elif "status" not in entry:
            msg = "Missing information status - verify dmg command output"
        elif "response" not in entry:
            msg = "No dmg {} scan 'response': status={}".format(
                key, entry["status"])
        elif section not in entry["response"]:
            msg = "No '{}' entry found in information 'response': {}".format(
                section, entry["response"])
        if msg:
            self.log.error(msg)
            raise ServerFailed("ServerInformation: {}".format(msg))

    def get_storage_scan_info(self, host):
        """Get the storage scan information is for this host.

        Args:
            host (str): host for which to get the storage information

        Raises:
            ServerFailed: if output from the dmg storage scan is missing or not
                in the expected format

        Returns:
            dict: a dictionary of storage information for the host, e.g.
                    {
                        "bdev_list": ["0000:13:00.0"],
                        "scm_list": ["pmem0"],
                    }

        """
        self._check_information("storage", "HostStorage")

        data = {}
        try:
            for entry in self.storage["response"]["HostStorage"].values():
                if host not in NodeSet(entry["hosts"].split(":")[0]):
                    continue
                if entry["storage"]["nvme_devices"]:
                    for device in entry["storage"]["nvme_devices"]:
                        if "bdev_list" not in data:
                            data["bdev_list"] = []
                        data["bdev_list"].append(device["pci_addr"])
                if entry["storage"]["scm_namespaces"]:
                    for device in entry["storage"]["scm_namespaces"]:
                        if "scm_list" not in data:
                            data["scm_list"] = []
                        data["scm_list"].append(device["blockdev"])
        except KeyError as error:
            raise ServerFailed(
                "ServerInformation: Error obtaining storage data") from error

        return data

    def get_network_scan_info(self, host):
        """Get the network scan information is for this host.

        Args:
            host (str): host for which to get the network information

        Raises:
            ServerFailed: if output from the dmg network scan is missing or not
                in the expected format

        Returns:
            dict: a dictionary of network information for the host, e.g.
                    {
                        1: {"fabric_iface": ib0, "provider": "ofi+psm2"},
                        2: {"fabric_iface": ib0, "provider": "ofi+verbs"},
                        3: {"fabric_iface": ib0, "provider": "ofi+tcp"},
                    }

        """
        self._check_information("network", "HostFabrics")

        data = {}
        try:
            for entry in self.network["response"]["HostFabrics"].values():
                if host not in NodeSet(entry["HostSet"].split(":")[0]):
                    continue
                if entry["HostFabric"]["Interfaces"]:
                    for device in entry["HostFabric"]["Interfaces"]:
                        # List each device/provider combo under its priority
                        data[device["Priority"]] = {
                            "fabric_iface": device["Device"],
                            "provider": device["Provider"],
                            "numa": device["NumaNode"]}
        except KeyError as error:
            raise ServerFailed(
                "ServerInformation: Error obtaining network data") from error

        return data

    def get_storage_capacity(self, engine_params):
        """Get the configured SCM and NVMe storage per server engine.

        Only sums up capacities of devices that have been specified in the
        server configuration file.

        Args:
            engine_params (list): a list of configuration parameters for each
                engine

        Raises:
            ServerFailed: if output from the dmg storage scan is missing or
                not in the expected format

        Returns:
            dict: a dictionary of each engine's smallest SCM and NVMe storage
                capacity in bytes, e.g.
                    {
                        "scm":  [3183575302144, 6367150604288],
                        "nvme": [1500312748032, 1500312748032]
                    }

        """
        self._check_information("storage", "HostStorage")

        device_capacity = {"nvme": {}, "scm": {}}
        try:
            for entry in self.storage["response"]["HostStorage"].values():
                # Collect a list of sizes for each NVMe device
                if entry["storage"]["nvme_devices"]:
                    for device in entry["storage"]["nvme_devices"]:
                        if device["pci_addr"] not in device_capacity["nvme"]:
                            device_capacity["nvme"][device["pci_addr"]] = []
                        device_capacity["nvme"][device["pci_addr"]].append(0)
                        for namespace in device["namespaces"]:
                            device_capacity["nvme"][device["pci_addr"]][-1] += \
                                namespace["size"]

                # Collect a list of sizes for each SCM device
                if entry["storage"]["scm_namespaces"]:
                    for device in entry["storage"]["scm_namespaces"]:
                        if device["blockdev"] not in device_capacity["scm"]:
                            device_capacity["scm"][device["blockdev"]] = []
                        device_capacity["scm"][device["blockdev"]].append(
                            device["size"])

        except KeyError as error:
            raise ServerFailed(
                "ServerInformation: Error obtaining storage data") from error

        self.log.info("Detected device capacities:")
        for category in sorted(device_capacity):
            for device in sorted(device_capacity[category]):
                sizes = [
                    get_display_size(size)
                    for size in device_capacity[category][device]]
                self.log.info(
                    "  %-4s for %s : %s", category.upper(), device, sizes)

        # Determine what storage is currently configured for each engine
        storage_capacity = {"scm": [], "nvme": []}
        for engine_param in engine_params:
            # Get the NVMe storage configuration for this engine
            bdev_list = engine_param.get_value("bdev_list")
            storage_capacity["nvme"].append(0)
            for device in bdev_list:
                if device in device_capacity["nvme"]:
                    storage_capacity["nvme"][-1] += min(
                        device_capacity["nvme"][device])

            # Get the SCM storage configuration for this engine
            scm_size = engine_param.get_value("scm_size")
            scm_list = engine_param.get_value("scm_list")
            if scm_list:
                storage_capacity["scm"].append(0)
                for device in scm_list:
                    scm_dev = os.path.basename(device)
                    if scm_dev in device_capacity["scm"]:
                        storage_capacity["scm"][-1] += min(
                            device_capacity["scm"][scm_dev])
            else:
                storage_capacity["scm"].append(
                    human_to_bytes("{}GB".format(scm_size)))

        self.log.info("Detected engine capacities:")
        for category in sorted(storage_capacity):
            sizes = [
                get_display_size(size) for size in storage_capacity[category]]
            self.log.info("  %-4s : %s", category.upper(), sizes)

        return storage_capacity
