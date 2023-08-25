"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import os
import re

from ClusterShell.NodeSet import NodeSet

from command_utils_base import FormattedParameter, CommandWithParameters
from command_utils import YamlCommand, CommandWithSubCommand
from data_utils import dict_extract_values, list_flatten
from dmg_utils import get_dmg_response
from exception_utils import CommandFailure
from general_utils import get_display_size


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class AutosizeCancel(Exception):
    """Cancel a test due to the inability to autosize a pool parameter."""


class DaosServerCommand(YamlCommand):
    """Defines an object representing the daos_server command."""

    NORMAL_PATTERN = "DAOS I/O Engine.*started"
    FORMAT_PATTERN = "(SCM format required)(?!;)"
    REFORMAT_PATTERN = "Metadata format required"
    SYSTEM_QUERY_PATTERN = "joined"

    DEFAULT_CONFIG_FILE = os.path.join(os.sep, "etc", "daos", "daos_server.yml")

    def __init__(self, path="", yaml_cfg=None, timeout=45):
        """Create a daos_server command object.

        Args:
            path (str): path to location of daos_server binary.
            yaml_cfg (YamlParameters, optional): yaml configuration parameters.
                Defaults to None.
            timeout (int, optional): number of seconds to wait for patterns to
                appear in the subprocess output. Defaults to 45 seconds.
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
        self.json = FormattedParameter("--json", False)
        self.config = FormattedParameter("--config={}", default_yaml_file)
        # Additional daos_server command line parameters:
        #     --allow-proxy  Allow proxy configuration via environment
        self.allow_proxy = FormattedParameter("--allow-proxy", False)

        # Used to override the sub_command.value parameter value
        self.sub_command_override = None

        # Include the daos_engine command launched by the daos_server
        # command.
        self._exe_names.append("daos_engine")

        # Include bullseye coverage file environment
        self.env["COVFILE"] = os.path.join(os.sep, "tmp", "test.cov")

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
        #   dump-topology  Dump system topology
        #   ms             Perform tasks related to management service replicas
        #   network        Perform network device scan based on fabric provider
        #   nvme           Perform tasks related to locally-attached NVMe storage
        #   scm            Perform tasks related to locally-attached SCM storage
        #   start          Start daos_server
        #   storage        Perform tasks related to locally-attached storage
        #   support        Perform tasks related to debug the system to help support team
        #   version        Print daos_server version
        if sub_command == "ms":
            self.sub_command_class = self.MsSubCommand()
        elif sub_command == "network":
            self.sub_command_class = self.NetworkSubCommand()
        elif sub_command == "nvme":
            self.sub_command_class = self.NvmeSubCommand()
        elif sub_command == "scm":
            self.sub_command_class = self.ScmSubCommand()
        elif sub_command == "start":
            self.sub_command_class = self.StartSubCommand()
        elif sub_command == "storage":
            self.sub_command_class = self.StorageSubCommand()
        elif sub_command == "support":
            self.sub_command_class = self.SupportSubCommand()
        elif sub_command == "version":
            self.sub_command_class = self.VersionSubCommand()
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
        elif mode == "dmg":
            self.pattern = self.SYSTEM_QUERY_PATTERN
        else:
            self.pattern = self.NORMAL_PATTERN
        self.pattern_count = host_qty * len(self.yaml.engine_params)

    def update_pattern_timeout(self):
        """Update the pattern timeout if undefined."""
        if self.pattern_timeout.value is None:
            self.log.debug('Updating pattern timeout based upon server config')
            try:
                data = self.yaml.get_yaml_data()
                bdev_lists = list_flatten(dict_extract_values(data, ['storage', '*', 'bdev_list']))
                self.log.debug('  Detected bdev_list entries: %s', bdev_lists)
            except (AttributeError, TypeError, RecursionError):
                # Default if the bdev_list cannot be obtained from the server configuration
                bdev_lists = []
            self.pattern_timeout.update(
                max(len(bdev_lists), 2) * 20, 'DaosServerCommand.pattern_timeout')

    @property
    def using_nvme(self):
        """Is the server command setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured; False otherwise

        """
        if self.yaml is not None and hasattr(self.yaml, "using_nvme"):
            return self.yaml.using_nvme
        return False

    @property
    def using_dcpm(self):
        """Is the server command setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured; False otherwise

        """
        if self.yaml is not None and hasattr(self.yaml, "using_dcpm"):
            return self.yaml.using_dcpm
        return False

    @property
    def using_control_metadata(self):
        """Is the server command setup to use a control plane metadata.

        Returns:
            bool: True if a control metadata path is being used; False otherwise
        """
        if self.yaml is not None and hasattr(self.yaml, "using_control_metadata"):
            return self.yaml.using_control_metadata
        return False

    @property
    def engine_params(self):
        """Get the configuration parameters for each server engine.

        Returns:
            list: a list of YamlParameters for each server engine

        """
        if self.yaml is not None and hasattr(self.yaml, "engine_params"):
            return self.yaml.engine_params
        return []

    @property
    def control_metadata(self):
        """Get the control plane metadata configuration parameters.

        Returns:
            ControlMetadataParameters: the control plane metadata configuration parameters or None
                if not defined.
        """
        if self.yaml is not None and hasattr(self.yaml, "metadata_params"):
            return self.yaml.metadata_params
        return None

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

    class MsSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server ms sub command."""

        def __init__(self):
            """Create an ms subcommand object."""
            super().__init__("/run/daos_server/ms/*", "ms")

        def get_sub_command_class(self):
            """Get the daos_server ms sub command object."""
            # Available daos_server ms sub-commands:
            #   recover  Recover the management service using this replica
            #   restore  Restore the management service from a snapshot
            #   status   Show status of the local management service replica
            if self.sub_command.value == "recover":
                self.sub_command_class = self.RecoverSubCommand()
            elif self.sub_command.value == "restore":
                self.sub_command_class = self.RestoreSubCommand()
            elif self.sub_command.value == "status":
                self.sub_command_class = self.StatusSubCommand()

        class RecoverSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server ms recover command."""

            def __init__(self):
                """Create a ms recover subcommand object."""
                super().__init__("/run/daos_server/ms/recover/*", "recover")

                # daos_server ms recover command options:
                #   -f, --force     Don't prompt for confirmation
                self.force = FormattedParameter("--force", False)

        class RestoreSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server ms restore command."""

            def __init__(self):
                """Create a ms restore subcommand object."""
                super().__init__("/run/daos_server/ms/restore/*", "restore")

                # daos_server ms restore command options:
                #   -f, --force     Don't prompt for confirmation
                #   -p, --path=     Path to snapshot file
                self.force = FormattedParameter("--force", False)
                self.path = FormattedParameter("--path={}")

        class StatusSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server ms status command."""

            def __init__(self):
                """Create a ms status subcommand object."""
                super().__init__("/run/daos_server/ms/status/*", "status")

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

    class NvmeSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server nvme sub command."""

        def __init__(self):
            """Create a daos_server nvme subcommand object."""
            super().__init__("/run/daos_server/nvme/*", "nvme")

        def get_sub_command_class(self):
            """Get the daos_server nvme sub command object."""
            # Available sub-commands:
            #   prepare  Prepare NVMe SSDs for use by DAOS
            #   reset    Reset NVMe SSDs for use by OS
            #   scan     Scan NVMe SSDs
            if self.sub_command.value == "prepare":
                self.sub_command_class = self.PrepareSubCommand()
            elif self.sub_command.value == "reset":
                self.sub_command_class = self.ResetSubCommand()
            elif self.sub_command.value == "scan":
                self.sub_command_class = self.ScanSubCommand()
            else:
                self.sub_command_class = None

        class PrepareSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server nvme prepare command."""

            def __init__(self):
                """Create a daos_server nvme prepare subcommand object."""
                super().__init__("/run/daos_server/nvme/prepare/*", "prepare")

                # daos_server nvme prepare command options:
                #   --helper-log-file=  Log file location for debug from daos_admin binary
                #   --ignore-config     Ignore parameters set in config file when running command
                #   --pci-block-list=   Comma-separated list of PCI devices (by address) to be
                #                       ignored when unbinding devices from Kernel driver to be
                #                       used with SPDK (default is no PCI devices)
                #   --hugepages=        Number of hugepages to allocate for use by SPDK
                #                       (default 1024)
                #   --target-user=      User that will own hugepage mountpoint directory and vfio
                #                       groups.
                #   --disable-vfio      Force SPDK to use the UIO driver for NVMe device access
                self.helper_log_file = FormattedParameter("--helper-log-file={}")
                self.ignore_config = FormattedParameter("--ignore-config", False)
                self.pci_block_list = FormattedParameter("--pci-block-list={}")
                self.hugepages = FormattedParameter("--hugepages={}")
                self.target_user = FormattedParameter("--target-user={}")
                self.disable_vfio = FormattedParameter("--disable-vfio", False)

        class ResetSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server nvme reset command."""

            def __init__(self):
                """Create a daos_server nvme reset subcommand object."""
                super().__init__("/run/daos_server/nvme/reset/*", "reset")

                # daos_server nvme reset command options:
                #   --helper-log-file= Log file location for debug from daos_server_helper binary
                #   --ignore-config    Ignore parameters set in config file when running command
                #   --pci-block-list=  Comma-separated list of PCI devices (by address) to be
                #                      ignored when unbinding devices from Kernel driver to be used
                #                      with SPDK (default is no PCI devices)
                #   --target-user=     User that will own hugepage mountpoint directory and vfio
                #                      groups.
                #   --disable-vfio     Force SPDK to use the UIO driver for NVMe device access
                self.helper_log_file = FormattedParameter("--helper-log-file={}")
                self.ignore_config = FormattedParameter("--ignore-config", False)
                self.pci_block_list = FormattedParameter("--pci-block-list={}")
                self.target_user = FormattedParameter("--target-user={}")
                self.disable_vfio = FormattedParameter("--disable-vfio", False)

        class ScanSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server nvme scan command."""

            def __init__(self):
                """Create a daos_server nvme scan subcommand object."""
                super().__init__(
                    "/run/daos_server/nvme/scan/*", "scan")

                # daos_server nvme scan command options:
                #   --helper-log-file=  Log file location for debug from daos_admin binary
                #   --ignore-config     Ignore parameters set in config file when running command
                #   --disable-vmd       Disable VMD-aware scan.
                self.helper_log_file = FormattedParameter("--helper-log-file={}")
                self.ignore_config = FormattedParameter("--ignore-config", False)
                self.disable_vmd = FormattedParameter("--disable-vmd", False)

    class ScmSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server scm sub command."""

        def __init__(self):
            """Create a daos_server scm subcommand object."""
            super().__init__("/run/daos_server/scm/*", "scm")

        def get_sub_command_class(self):
            """Get the daos_server scm sub command object."""
            # Available sub-commands:
            #   prepare  Prepare SCM devices so that they can be used with DAOS
            #   reset    Reset SCM devices that have been used with DAOS
            #   scan     Scan SCM devices
            if self.sub_command.value == "prepare":
                self.sub_command_class = self.PrepareSubCommand()
            elif self.sub_command.value == "reset":
                self.sub_command_class = self.ResetSubCommand()
            elif self.sub_command.value == "scan":
                self.sub_command_class = self.ScanSubCommand()
            else:
                self.sub_command_class = None

        class PrepareSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server scm prepare command."""

            def __init__(self):
                """Create a daos_server scm prepare subcommand object."""
                super().__init__("/run/daos_server/scm/prepare/*", "prepare")

                # daos_server scm prepare command options:
                #   --helper-log-file=   Log file location for debug from daos_admin binary
                #   --ignore-config      Ignore parameters set in config file when running command
                #   --socket=            Perform PMem namespace operations on the socket
                #                        identified by this ID (defaults to all sockets). PMem
                #                        region operations will be performed across all sockets.
                #   --scm-ns-per-socket= Number of PMem namespaces to create per socket (default:1)
                #   --force              Perform SCM operations without waiting for confirmation
                self.helper_log_file = FormattedParameter("--helper-log-file={}")
                self.ignore_config = FormattedParameter("--ignore-config", False)
                self.socket = FormattedParameter("--socket={}")
                self.scm_ns_per_socket = FormattedParameter("--scm-ns-per-socket={}")
                self.force = FormattedParameter("--force", False)

        class ResetSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server scm reset command."""

            def __init__(self):
                """Create a daos_server scm reset subcommand object."""
                super().__init__("/run/daos_server/scm/reset/*", "reset")

                # daos_server scm reset command options:
                #   --helper-log-file=   Log file location for debug from daos_admin binary
                #   --ignore-config      Ignore parameters set in config file when running command
                #   --socket=            Perform PMem namespace operations on the socket
                #                        identified by this ID (defaults to all sockets). PMem
                #                        region operations will be performed across all sockets.
                #   --force              Perform SCM operations without waiting for confirmation
                self.helper_log_file = FormattedParameter("--helper-log-file={}")
                self.ignore_config = FormattedParameter("--ignore-config", False)
                self.socket = FormattedParameter("--socket={}")
                self.force = FormattedParameter("--force", False)

        class ScanSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server scm scan command."""

            def __init__(self):
                """Create a daos_server scm scan subcommand object."""
                super().__init__("/run/daos_server/scm/scan/*", "scan")

                # daos_server scm scan command option:
                #   --helper-log-file=   Log file location for debug from daos_admin binary
                self.helper_log_file = FormattedParameter("--helper-log-file={}")

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
                """Create a storage prepare subcommand object."""
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
                #   --scm-ns-per-socket Number of SCM namespaces to create per socket (default: 1)
                self.pci_allowlist = FormattedParameter("--pci-allowlist={}")
                self.hugepages = FormattedParameter("--hugepages={}")
                self.target_user = FormattedParameter("--target-user={}")
                self.nvme_only = FormattedParameter("--nvme-only", False)
                self.scm_only = FormattedParameter("--scm-only", False)
                self.reset = FormattedParameter("--reset", False)
                self.force = FormattedParameter("--force", False)
                self.scm_ns_per_socket = FormattedParameter("--scm-ns-per-socket={}")

    class SupportSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server support sub command."""

        def __init__(self):
            """Create a support subcommand object."""
            super().__init__("/run/daos_server/support/*", "support")

        def get_sub_command_class(self):
            """Get the daos_server support sub command object."""
            # Available sub-commands:
            #   collect-log  Collect logs on servers
            if self.sub_command.value == "collect-log":
                self.sub_command_class = self.CollectLogSubCommand()
            else:
                self.sub_command_class = None

        class CollectLogSubCommand(CommandWithSubCommand):
            """Defines an object for the daos_server support collect-log command."""

            def __init__(self):
                """Create a support collect-log subcommand object."""
                super().__init__(
                    "/run/daos_server/support/collect-log/*", "collect-log")

                # daos_server support collect-log command options:
                #   --stop-on-error     Stop the collect-log command on very first error
                #   --target-folder=    Target Folder location where log will be copied
                #   --archive=          Archive the log/config files
                #   --extra-logs-dir=   Collect the Logs from given custom directory
                #   --target-host=      R sync the logs to target-host system
                self.stop_on_error = FormattedParameter("--stop-on-error", False)
                self.target_folder = FormattedParameter("--target-folder={}")
                self.archive = FormattedParameter("--archive", False)
                self.extra_logs_dir = FormattedParameter("--extra-logs-dir={}")
                self.target_host = FormattedParameter("--target-host={}")

    class VersionSubCommand(CommandWithSubCommand):
        """Defines an object for the daos_server version sub command."""

        def __init__(self):
            """Create a version subcommand object."""
            super().__init__("/run/daos_server/version/*", "version")


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
                        1: {"fabric_iface": ib0, "provider": "ofi+verbs"},
                        2: {"fabric_iface": ib0, "provider": "ofi+tcp"},
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
        mount_total_bytes = self.get_scm_mount_total_bytes()
        storage_capacity = {"scm": [], "nvme": []}
        for engine_param in engine_params:
            # Get the NVMe storage configuration for this engine
            bdev_list = engine_param.get_value("bdev_list")
            storage_capacity["nvme"].append(0)
            for device in bdev_list:
                if device in device_capacity["nvme"]:
                    storage_capacity["nvme"][-1] += min(device_capacity["nvme"][device])
                else:
                    # VMD controlled devices include the controller address at the beginning of
                    # their address, e.g. "0000:85:05.5" -> "850505:01:00.0"
                    address_split = [int(x, 16) for x in re.split(r":|\.", device)]
                    vmd_device = "{1:02x}{2:02x}{3:02x}:".format(*address_split)
                    for controller in device_capacity["nvme"]:
                        if controller.startswith(vmd_device):
                            storage_capacity["nvme"][-1] += min(device_capacity["nvme"][controller])

            # Get the SCM storage configuration for this engine
            scm_mount = engine_param.get_value("scm_mount")
            scm_list = engine_param.get_value("scm_list")
            if scm_list:
                storage_capacity["scm"].append(0)
                for device in scm_list:
                    scm_dev = os.path.basename(device)
                    if scm_dev in device_capacity["scm"]:
                        storage_capacity["scm"][-1] += min(
                            device_capacity["scm"][scm_dev])
            elif scm_mount in mount_total_bytes:
                storage_capacity["scm"].append(mount_total_bytes[scm_mount])
            else:
                storage_capacity["scm"].append(0)

        self.log.info("Detected engine capacities:")
        for category in sorted(storage_capacity):
            sizes = [
                get_display_size(size) for size in storage_capacity[category]]
            self.log.info("  %-4s : %s", category.upper(), sizes)

        return storage_capacity

    def get_scm_mount_total_bytes(self):
        """Get the total size of each SCM mount.

        Returns:
            dict: total bytes value for each SCM mount key.
        """
        try:
            results = get_dmg_response(self.dmg.storage_query_usage)
        except CommandFailure as error:
            raise ServerFailed("ServerInformation: Error obtaining configured storage") from error

        mount_total_bytes = {}
        for host_storage in results["HostStorage"].values():
            for scm_namespace in host_storage["storage"]["scm_namespaces"]:
                mount = scm_namespace["mount"]["path"]
                mount_total_bytes[mount] = scm_namespace["mount"]["total_bytes"]

        return mount_total_bytes


class DaosServerCommandRunner(DaosServerCommand):
    """Defines a object representing a daos_server command."""

    def __init__(self, path):
        """Create a daos_server Command object.

        Args:
            path (str): path to the daos_server command
        """
        super().__init__(path)

        self.debug.value = False
        self.json_logs.value = False

    def recover(self, force=False):
        """Call daos_server ms recover.

        Args:
            force (bool, optional): Don't prompt for confirmation. Defaults to False.

        Returns:
            CmdResult: an avocado CmdResult object containing the daos_server command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the daos_server recover command fails.

        """
        return self._get_result(["ms", "recover"], force=force)

    def restore(self, force=False, path=None):
        """Call daos_server ms restore.

        Args:
            force (bool, optional): Don't prompt for confirmation. Defaults to False.
            path (str, optional): Path to snapshot file. Defaults to None.

        Returns:
            CmdResult: an avocado CmdResult object containing the daos_server command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the daos_server restore command fails.

        """
        return self._get_result(["ms", "restore"], force=force, path=path)

    def status(self):
        """Call daos_server ms status.

        Returns:
            CmdResult: an avocado CmdResult object containing the daos_server command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the daos_server status command fails.

        """
        return self._get_result(["ms", "status"])

    def version(self):
        """Call daos_server version.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos_server version command fails.

        """
        return self._get_json_result(("version",))
