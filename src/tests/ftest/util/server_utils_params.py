"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from command_utils_base import \
    BasicParameter, LogParameter, YamlParameters, TransportCredentials

MAX_STORAGE_TIERS = 5


class DaosServerTransportCredentials(TransportCredentials):
    # pylint: disable=too-few-public-methods
    """Transport credentials listing certificates for secure communication."""

    def __init__(self, log_dir=os.path.join(os.sep, "tmp")):
        """Initialize a TransportConfig object."""
        super().__init__("/run/server_config/transport_config/*", "transport_config", log_dir)

        # Additional daos_server transport credential parameters:
        #   - client_cert_dir: <str>, e.g. "".daos/clients"
        #       Location of client certificates [daos_server only]
        #
        self.client_cert_dir = LogParameter(self._log_dir, None, "clients")
        self.cert = LogParameter(self._log_dir, None, "server.crt")
        self.key = LogParameter(self._log_dir, None, "server.key")

    def get_certificate_data(self, name_list):
        """Get certificate data.

        Args:
            name_list (list): list of certificate attribute names.

        Returns:
            data (dict): a dictionary of parameter directory name keys and
                value.

        """
        # Ensure the client cert directory includes the required certificate
        name_list.remove("client_cert_dir")
        data = super().get_certificate_data(name_list)
        if not self.allow_insecure.value and self.client_cert_dir.value:
            if self.client_cert_dir.value not in data:
                data[self.client_cert_dir.value] = ["agent.crt"]
            else:
                data[self.client_cert_dir.value].append("agent.crt")
        return data

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            DaosServerTransportCredentials: a new DaosServerTransportCredentials object
        """
        return DaosServerTransportCredentials(self._log_dir)


class DaosServerYamlParameters(YamlParameters):
    """Defines the daos_server configuration yaml parameters."""

    def __init__(self, filename, common_yaml):
        """Initialize an DaosServerYamlParameters object.

        Args:
            filename (str): yaml configuration file name
            common_yaml (YamlParameters): [description]
        """
        super().__init__("/run/server_config/*", filename, None, common_yaml)

        # pylint: disable=wrong-spelling-in-comment
        # daos_server configuration file parameters
        #
        #   - provider: <str>, e.g. ofi+verbs;ofi_rxm
        #       Force a specific provider to be used by all the servers.
        #
        #   - hyperthreads: <bool>, e.g. True
        #       When Hyperthreading is enabled and supported on the system, this
        #       parameter defines whether the DAOS service thread should only be
        #       bound to different physical cores (False) or hyperthreads (True)
        #
        #   - socket_dir: <str>, e.g. /var/run/daos_server
        #       DAOS Agent and DAOS Server both use unix domain sockets for
        #       communication with other system components. This setting is the
        #       base location to place the sockets in.
        #
        #   - nr_hugepages: <int>, e.g. 4096
        #       Number of hugepages to allocate for use with SPDK and NVMe SSDs.
        #       This value is only used for optional override and will be
        #       automatically calculated if unset.
        #
        #   - control_log_mask: <str>, e.g. DEBUG
        #       Force specific debug mask for daos_server (control plane).
        #
        #   - control_log_file: <str>, e.g. /tmp/daos_control.log
        #       Force specific path for daos_server (control plane) logs.
        #
        #   - user_name: <str>, e.g. daosuser
        #       Username used to lookup user uid/gid to drop privileges to if
        #       started as root. After control plane start-up and configuration,
        #       before starting data plane, process ownership will be dropped to
        #       those of supplied user.
        #
        #   - group_name: <str>, e.g. daosgroup
        #       Group name used to lookup group gid to drop privileges to when
        #       user_name is root. If user is a member of group, this group gid
        #       is set for the running process. If group look up fails or user
        #       is not member, use uid return from user lookup.
        #
        default_provider = os.environ.get("CRT_PHY_ADDR_STR", "ofi+sockets")

        # All log files should be placed in the same directory on each host to
        # enable easy log file archiving by launch.py
        log_dir = os.environ.get("DAOS_TEST_LOG_DIR", os.path.join(os.sep, "tmp"))

        self.provider = BasicParameter(None, default_provider)
        self.crt_ctx_share_addr = BasicParameter(None)
        self.crt_timeout = BasicParameter(None, 10)
        self.disable_srx = BasicParameter(None)
        self.fabric_auth_key = BasicParameter(None)
        self.core_dump_filter = BasicParameter(None)
        self.bdev_exclude = BasicParameter(None)
        self.disable_vfio = BasicParameter(None)
        self.disable_vmd = BasicParameter(None)
        self.enable_hotplug = BasicParameter(None)
        self.hyperthreads = BasicParameter(None, False)
        self.socket_dir = BasicParameter(None, "/var/run/daos_server")
        # Auto-calculate if unset or set to zero
        self.nr_hugepages = BasicParameter(None, 0)
        self.disable_hugepages = BasicParameter(None)
        self.system_ram_reserved = BasicParameter(None)
        self.control_log_mask = BasicParameter(None, "DEBUG")
        self.control_log_file = LogParameter(log_dir, None, "daos_control.log")
        self.helper_log_file = LogParameter(log_dir, None, "daos_server_helper.log")
        self.telemetry_port = BasicParameter(None, 9191)
        self.client_env_vars = BasicParameter(None)

        # Used to drop privileges before starting data plane
        # (if started as root to perform hardware provisioning)
        self.user_name = BasicParameter(None)
        self.group_name = BasicParameter(None)

        # Control plane metadata parameters.
        self.metadata_params = ControlMetadataParameters(self.namespace)

        # Defines the number of single engine config parameters to define in
        # the yaml file
        self.engines_per_host = BasicParameter(None, 0)

        # Single engine config parameters. Default to one set of I/O Engine
        # parameters - for the config_file_gen.py tool. Calling get_params()
        # will update the list to match the number of I/O Engines requested by
        # the self.engines_per_host.value.
        self.engine_params = [EngineYamlParameters(self.namespace, 0)]

        # Maximum number of detectable storage tiers. This can be used to limit what is read from
        # test yaml. If changed, get_params() should be called.
        self.max_storage_tiers = MAX_STORAGE_TIERS

        # Fault domain path & callback
        self.fault_path = BasicParameter(None)
        self.fault_cb = BasicParameter(None)

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)

        # Get the optional control plane metadata parameters
        self.metadata_params.get_params(test)

        # Create the requested number of single server parameters
        self.engine_params = []
        for index in range(self.engines_per_host.value or 0):
            self.engine_params.append(
                EngineYamlParameters(
                    self.namespace, index, self.provider.value, self.max_storage_tiers))
            self.engine_params[-1].get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        # Get the common config yaml parameters
        yaml_data = super().get_yaml_data()

        # Remove the "engines_per_host" BasicParameter as it is not an actual
        # daos_server configuration file parameter
        yaml_data.pop("engines_per_host", None)

        # Add the optional control plane metadata parameters
        metadata_yaml = self.metadata_params.get_yaml_data()
        if metadata_yaml:
            yaml_data["control_metadata"] = metadata_yaml

        # Add the per-engine yaml parameters
        yaml_data["engines"] = []
        for engine_params in self.engine_params:
            yaml_data["engines"].append(engine_params.get_yaml_data())

        return yaml_data

    def is_yaml_data_updated(self):
        """Determine if any of the yaml file parameters have been updated.

        Returns:
            bool: whether or not a yaml file parameter has been updated

        """
        yaml_data_updated = super().is_yaml_data_updated()
        if not yaml_data_updated:
            for other_params in self.engine_params + [self.metadata_params]:
                if other_params.is_yaml_data_updated():
                    yaml_data_updated = True
                    break
        return yaml_data_updated

    def reset_yaml_data_updated(self):
        """Reset each yaml file parameter updated state to False."""
        super().reset_yaml_data_updated()
        for other_params in self.engine_params + [self.metadata_params]:
            other_params.reset_yaml_data_updated()

    def set_value(self, name, value):
        """Set the value for a specified attribute name.

        Args:
            name (str): name of the attribute for which to set the value
            value (object): the value to set

        Returns:
            bool: if the attribute name was found and the value was set

        """
        status = super().set_value(name, value)

        # Set the value for each per-engine configuration attribute name
        if not status:
            for other_params in self.engine_params + [self.metadata_params]:
                if other_params.set_value(name, value):
                    status = True

        return status

    def get_value(self, name):
        """Get the value of the specified attribute name.

        Args:
            name (str): name of the attribute from which to get the value

        Returns:
            object: the object's value referenced by the attribute name

        """
        value = super().get_value(name)

        # Look for the value in the per-engine configuration parameters.  The
        # first value found will be returned.
        index = 0
        other_params = self.engine_params + [self.metadata_params]
        while value is None and index < len(other_params):
            value = other_params[index].get_value(name)
            index += 1

        return value

    def get_engine_values(self, name):
        """Get the value of the specified attribute name for each engine.

        Args:
            name (str): name of the attribute from which to get the value

        Returns:
            list: a list of the value of each matching configuration attribute
                name per engine

        """
        engine_values = []
        for engine_params in self.engine_params:
            engine_values.append(engine_params.get_value(name))
        return engine_values

    @property
    def using_nvme(self):
        """Is the configuration file setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured for at least one server in
                the config file; False otherwise

        """
        for engine_params in self.engine_params:
            if engine_params.using_nvme:
                return True
        return False

    @property
    def using_dcpm(self):
        """Is the configuration file setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured for at least one server in
                the config file; False otherwise

        """
        for engine_params in self.engine_params:
            if engine_params.using_dcpm:
                return True
        return False

    @property
    def using_control_metadata(self):
        """Is the configuration file setup to use a control plane metadata.

        Returns:
            bool: True if a control metadata path is being used; False otherwise
        """
        return self.metadata_params.path.value is not None

    def update_log_files(self, control_log, helper_log, server_log):
        """Update the logfile parameter for the daos server.

        If there are multiple engine configurations defined the server_log value
        will be made unique for each engine's log_file parameter.

        Any log file name set to None will result in no update to the respective
        log file parameter value.

        Args:
            control_log (str): control log file name
            helper_log (str): helper (admin) log file name
            server_log (str): per engine log file name
        """
        if control_log is not None:
            self.control_log_file.update(
                control_log, "server_config.control_log_file")
        if helper_log is not None:
            self.helper_log_file.update(
                helper_log, "server_config.helper_log_file")
        if server_log is not None:
            for index, engine_params in enumerate(self.engine_params):
                log_name = list(os.path.splitext(server_log))
                if len(self.engine_params) > 1:
                    # Create unique log file names for each I/O Engine
                    log_name.insert(1, "_{}".format(index))
                engine_params.log_file.update(
                    "".join(log_name),
                    "server_config.server[{}].log_file".format(index))

    def override_params(self, data):
        """Override the values of the daos server yaml config file with the external data.

        Args:
            data (dict): external server configuration data.
        """
        self.log.info("Overriding server config with external data")
        self.engine_params = []
        if "engines" in data:
            self.engines_per_host.update(len(data["engines"]), "engines_per_host")
            for engine, engine_data in enumerate(data["engines"]):
                self.engine_params.append(
                    EngineYamlParameters(
                        self.namespace, engine, data["provider"], self.max_storage_tiers))
                self.engine_params[-1].override_params(engine_data)
        else:
            self.engines_per_host.update(0, "engines_per_host")

        self.metadata_params = ControlMetadataParameters(self.namespace)
        if "control_metadata" in data:
            self.metadata_params.override_params(data["control_metadata"])

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            DaosServerYamlParameters: a new DaosServerYamlParameters object
        """
        return DaosServerYamlParameters(self.filename, None)


class ControlMetadataParameters(YamlParameters):
    """Defines the configuration yaml parameters for the control plane metadata."""

    def __init__(self, base_namespace):
        """Initialize an ControlMetadataParameters object.

        Args:
            base_namespace (str): namespace for the server configuration
        """
        namespace = [os.sep] + base_namespace.split(os.sep)[1:-1] + ["control_metadata", "*"]
        self._base_namespace = base_namespace
        super().__init__(os.path.join(*namespace))

        # Directory to store control plane metadata / device mountpoint (if device is specified)
        self.path = BasicParameter(None)

        # Storage partition to be formatted with an ext4 filesystem and mounted for metadata storage
        self.device = BasicParameter(None)

    def override_params(self, data):
        """Override the values of the daos server yaml config file with the external data.

        Args:
            data (dict): external server configuration data.
        """
        self.log.info("Overriding control metadata config with external data")
        for name in self.get_param_names():
            param = getattr(self, name)
            name = param.yaml_key or name
            if name in data:
                param.update(data[name], name)

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            ControlMetadataParameters: a new ControlMetadataParameters object
        """
        return ControlMetadataParameters(self.namespace)


class EngineYamlParameters(YamlParameters):
    """Defines the configuration yaml parameters for a single server engine."""

    # Engine environment variables that are required by provider type.
    REQUIRED_ENV_VARS = {
        "common": [
            "D_LOG_FILE_APPEND_PID=1",
            "COVFILE=/tmp/test.cov"],
        "ofi+tcp;ofi_rxm": [],
        "ofi+verbs": [
            "FI_OFI_RXM_USE_SRX=1"],
        "ofi+cxi": [
            "FI_OFI_RXM_USE_SRX=1",
            "CRT_MRC_ENABLE=1"],
    }

    def __init__(self, base_namespace, index, provider=None, max_storage_tiers=MAX_STORAGE_TIERS):
        """Create a SingleServerConfig object.

        Args:
            base_namespace (str): namespace for the server configuration
            index (int): engine index number for the namespace path
            provider (str, optional): index number for the namespace path used
                when specifying multiple engines per host. Defaults to None.
            max_storage_tiers (int, optional): maximum number of storage tiers that can be detected.
                Defaults to MAX_STORAGE_TIERS.
        """
        namespace = [os.sep] + base_namespace.split(os.sep)[1:-1] + ["engines", str(index), "*"]
        self._base_namespace = base_namespace
        self._index = index
        self._provider = provider or os.environ.get("CRT_PHY_ADDR_STR", "ofi+tcp;ofi_rxm")
        self._max_storage_tiers = max_storage_tiers
        super().__init__(os.path.join(*namespace))

        # Use environment variables to get default parameters
        default_interface = os.environ.get("DAOS_TEST_FABRIC_IFACE", "eth0")
        default_port = int(os.environ.get("OFI_PORT", 31416))
        default_share_addr = int(os.environ.get("CRT_CTX_SHARE_ADDR", 0))

        # All log files should be placed in the same directory on each host
        # to enable easy log file archiving by launch.py
        log_dir = os.environ.get("DAOS_TEST_LOG_DIR", os.path.join(os.sep, "tmp"))

        # Parameters
        #   targets:                I/O service threads per engine
        #   first_core:             starting index for targets
        #   nr_xs_helpers:          I/O offload threads per engine
        #   fabric_iface:           map to OFI_INTERFACE=eth0
        #   fabric_iface_port:      map to OFI_PORT=31416
        #   log_mask:               map to D_LOG_MASK env
        #   log_file:               map to D_LOG_FILE env
        #   env_vars:               influences DAOS I/O Engine behavior
        #       Add to enable scalable endpoint:
        #           - CRT_CTX_SHARE_ADDR=1
        #           - CRT_CTX_NUM=8
        self.targets = BasicParameter(None, 8)
        self.first_core = BasicParameter(None, 0)
        self.nr_xs_helpers = BasicParameter(None, 4)
        self.fabric_iface = BasicParameter(None, default_interface)
        self.fabric_iface_port = BasicParameter(None, default_port)
        self.pinned_numa_node = BasicParameter(None)
        self.log_mask = BasicParameter(None, "INFO")
        self.log_file = LogParameter(log_dir, None, "daos_server.log")

        # Set default environment variables
        default_env_vars = [
            "ABT_ENV_MAX_NUM_XSTREAMS=100",
            "ABT_MAX_NUM_XSTREAMS=100",
            "DAOS_MD_CAP=1024",
            "DAOS_SCHED_WATCHDOG_ALL=1",
            "DD_MASK=mgmt,io,md,epc,rebuild",
        ]
        default_env_vars.extend(self.REQUIRED_ENV_VARS["common"])
        for name in self._provider.split(";"):
            if name in self.REQUIRED_ENV_VARS:
                default_env_vars.extend(self.REQUIRED_ENV_VARS[name])
        self.env_vars = BasicParameter(None, default_env_vars)

        # global CRT_CTX_SHARE_ADDR shared with client
        self.crt_ctx_share_addr = BasicParameter(None, default_share_addr)

        # the storage configuration for this engine
        self.storage = StorageYamlParameters(self.namespace, max_storage_tiers)

    def get_params(self, test):
        """Get values for the daos server yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)

        # Override the log file file name with the test log file name
        if hasattr(test, "server_log") and test.server_log is not None:
            self.log_file.value = test.server_log

        # Create the requested number of storage tier parameters
        self.storage.get_params(test)

        # Define any required env vars
        required_env_vars = {}
        for env in self.REQUIRED_ENV_VARS["common"]:
            required_env_vars[env.split("=", maxsplit=1)[0]] = env.split("=", maxsplit=1)[1]
        for name in self._provider.split(";"):
            if name in self.REQUIRED_ENV_VARS:
                required_env_vars.update({
                    env.split("=", maxsplit=1)[0]: env.split("=", maxsplit=1)[1]
                    for env in self.REQUIRED_ENV_VARS[name]})

        # Enable fault injection if configured
        if test.fault_injection.fault_file is not None:
            self.log.debug("Enabling fault injection")
            required_env_vars["D_FI_CONFIG"] = test.fault_injection.fault_file

        # Update the env vars with any missing or different required setting
        update = False
        env_var_dict = {env.split("=")[0]: env.split("=")[1] for env in self.env_vars.value}
        for key in sorted(required_env_vars):
            if key not in env_var_dict or env_var_dict[key] != required_env_vars[key]:
                env_var_dict[key] = required_env_vars[key]
                update = True
        if update:
            self.log.debug("Assigning required env_vars")
            new_env_vars = ["=".join([key, str(value)]) for key, value in env_var_dict.items()]
            self.env_vars.update(new_env_vars, "env_var")

    @property
    def using_nvme(self):
        """Is the configuration file setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured; False otherwise

        """
        return self.storage.using_nvme

    @property
    def using_dcpm(self):
        """Is the configuration file setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured; False otherwise

        """
        return self.storage.using_dcpm

    def update_log_file(self, name):
        """Update the daos server log file parameter.

        Args:
            name (str): new log file name
        """
        self.log_file.update(name, "log_file")

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        # Get the common config yaml parameters
        yaml_data = super().get_yaml_data()

        # Add the storage tier yaml parameters
        yaml_data.update(self.storage.get_yaml_data())

        return yaml_data

    def is_yaml_data_updated(self):
        """Determine if any of the yaml file parameters have been updated.

        Returns:
            bool: whether or not a yaml file parameter has been updated

        """
        return super().is_yaml_data_updated() or self.storage.is_yaml_data_updated()

    def reset_yaml_data_updated(self):
        """Reset each yaml file parameter updated state to False."""
        super().reset_yaml_data_updated()
        self.storage.reset_yaml_data_updated()

    def set_value(self, name, value):
        """Set the value for a specified attribute name.

        Args:
            name (str): name of the attribute for which to set the value
            value (object): the value to set

        Returns:
            bool: if the attribute name was found and the value was set

        """
        status = super().set_value(name, value)

        # Set the value for each per-engine configuration attribute name
        if not status:
            status = self.storage.set_value(name, value)

        return status

    def get_value(self, name):
        """Get the value of the specified attribute name.

        Args:
            name (str): name of the attribute from which to get the value

        Returns:
            object: the object's value referenced by the attribute name

        """
        value = super().get_value(name)
        if value is None:
            # Look for the value in the storage tier parameters.  The first value found will be
            # returned.
            value = self.storage.get_value(name)

        return value

    def override_params(self, data):
        """Override the values of the daos server yaml config file with the external data.

        Args:
            data (dict): external engine configuration data.
        """
        self.log.info("Overriding engine %s config with external data", self._index)
        self.storage.override_params(data)

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            EngineYamlParameters: a new EngineYamlParameters object
        """
        return EngineYamlParameters(
            self._base_namespace, self._index, self._provider, self._max_storage_tiers)


class StorageYamlParameters(YamlParameters):
    """Defines the configuration yaml parameters for all of the storage tiers for an engine."""

    def __init__(self, base_namespace, max_tiers=MAX_STORAGE_TIERS):
        """Create a SingleServerConfig object.

        Args:
            base_namespace (str): namespace for the server engine configuration
            max_tiers (int, optional): maximum number of storage tiers that can be detected.
                Defaults to MAX_STORAGE_TIERS.
        """
        super().__init__(base_namespace)

        # Maximum number of detectable storage tiers. This can be used to limit what is read from
        # test yaml. If changed, DaosServerManager.get_params() should be called.
        self._max_tiers = max_tiers

        # Each engine can define one or more storage tiers. Default to one storage tier for this
        # engine - for the config_file_gen.py tool. Calling get_params() will update the list to
        # match what is specified by the test yaml.
        self.storage_tiers = [StorageTierYamlParameters(self.namespace, 0)]

    @property
    def using_nvme(self):
        """Is the configuration file setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured; False otherwise

        """
        return any(tier.using_nvme for tier in self.storage_tiers)

    @property
    def using_dcpm(self):
        """Is the configuration file setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured; False otherwise

        """
        return any(tier.using_dcpm for tier in self.storage_tiers)

    @property
    def scm_mount(self):
        """Get the scm_mount value from the scm storage tier.

        Returns:
            str: the scm_mount value or None

        """
        for tier in self.storage_tiers:
            if tier.storage_class.value in ["ram", "dcpm"]:
                return tier.scm_mount.value
        return None

    def get_params(self, test):
        """Get values for the daos server yaml config file.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)

        # Since the test.params.get() method does not return a dictionary from the test yaml, use
        # the required 'class' field to determine how many entries are in the test yaml
        self.storage_tiers = []
        while len(self.storage_tiers) < self._max_tiers:
            tier_index = len(self.storage_tiers)
            storage_tier_params = StorageTierYamlParameters(self.namespace, tier_index)
            self.log.debug("Checking for storage tier in %s ", storage_tier_params.namespace)
            if test.params.get("class", storage_tier_params.namespace, None) is None:
                break

            # Add the storage tier parameters
            self.storage_tiers.append(storage_tier_params)
            self.storage_tiers[-1].get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        # Get the common config yaml parameters
        yaml_data = super().get_yaml_data()

        # Add the storage tier yaml parameters
        storage_data = []
        for tier in self.storage_tiers:
            data = tier.get_yaml_data()
            if data:
                storage_data.append(data)
        if storage_data:
            yaml_data["storage"] = storage_data

        return yaml_data

    def is_yaml_data_updated(self):
        """Determine if any of the yaml file parameters have been updated.

        Returns:
            bool: whether or not a yaml file parameter has been updated

        """
        yaml_data_updated = super().is_yaml_data_updated()
        if not yaml_data_updated:
            for tier in self.storage_tiers:
                if tier.is_yaml_data_updated():
                    yaml_data_updated = True
                    break
        return yaml_data_updated

    def reset_yaml_data_updated(self):
        """Reset each yaml file parameter updated state to False."""
        super().reset_yaml_data_updated()
        for tier in self.storage_tiers:
            tier.reset_yaml_data_updated()

    def set_value(self, name, value):
        """Set the value for a specified attribute name.

        Args:
            name (str): name of the attribute for which to set the value
            value (object): the value to set

        Returns:
            bool: if the attribute name was found and the value was set

        """
        status = super().set_value(name, value)

        # Set the value for each storage tier attribute name
        if not status:
            for tier in self.storage_tiers:
                if tier.set_value(name, value):
                    status = True

        return status

    def get_value(self, name):
        """Get the value of the specified attribute name.

        Args:
            name (str): name of the attribute from which to get the value

        Returns:
            object: the object's value referenced by the attribute name

        """
        value = super().get_value(name)

        # Look for the value in the storage tier configuration parameters. The first value found
        # will be returned.
        index = 0
        while value is None and index < len(self.storage_tiers):
            value = self.storage_tiers[index].get_value(name)
            index += 1

        return value

    def override_params(self, data):
        """Override the values of the daos server yaml config file with the external data.

        Args:
            data (dict): external server configuration data.
        """
        self.storage_tiers = []
        if "storage" in data:
            for tier, storage_data in enumerate(data["storage"]):
                if "class" in storage_data:
                    self.storage_tiers.append(StorageTierYamlParameters(self.namespace, tier))
                    self.storage_tiers[-1].override_params(storage_data)

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            StorageYamlParameters: a new StorageYamlParameters object
        """
        return StorageYamlParameters(self.namespace, self._max_tiers)


class StorageTierYamlParameters(YamlParameters):
    """Defines the configuration yaml parameters for each storage tier for an engine."""

    def __init__(self, base_namespace, tier):
        """Create a SingleServerConfig object.

        Args:
            base_namespace (str): namespace for the server engine configuration
            tier (int) index number for the storage tier namespace path.
        """
        namespace = [os.sep] + base_namespace.split(os.sep)[1:-1] + ["storage", str(tier), "*"]
        self._base_namespace = base_namespace
        self._tier = tier
        super().__init__(os.path.join(*namespace))

        # Example storage tier definition in a test yaml file:
        #
        #   storage:
        #     0:
        #       class: ram
        #       scm_mount: /mnt/daos
        #       scm_size: 16
        #
        # Example storage tier definition in a test yaml file:
        #
        #   storage:
        #     0:
        #       class: dcpm
        #       scm_mount: /mnt/daos
        #       scm_list: [/dev/pmem0]
        #     1:
        #       class: bdev
        #       bdev_list: ["aaaa.aa.aa.a", "bbbb.bb.bb.b"]
        #       bdev_number: 2
        #       bdev_size: 20
        #
        # Two StorageTierYamlParameters objects would be used for the second example.
        #
        self.storage_class = BasicParameter(None, yaml_key="class", position=1)

        # Additional 'class: dcpm' options
        self.scm_list = BasicParameter(None, position=2)

        # Additional 'class: dcpm|ram' options
        self.scm_mount = BasicParameter(None, position=3)

        # Additional 'class: ram' options
        self.scm_size = BasicParameter(None, position=4)
        self.scm_hugepages_disabled = BasicParameter(None, position=5)

        # Additional 'class: bdev' options
        self.bdev_list = BasicParameter(None, position=6)
        self.bdev_number = BasicParameter(None, position=7)
        self.bdev_size = BasicParameter(None, position=8)

        # Additional 'class: bdev' options when storage tier 0 is 'class: ram'
        self.bdev_roles = BasicParameter(None, position=9)

    @property
    def using_dcpm(self):
        """Is the configuration file setup to use SCM devices.

        Returns:
            bool: True if SCM devices are configured; False otherwise

        """
        return self.storage_class.value == "dcpm"

    @property
    def using_nvme(self):
        """Is the configuration file setup to use NVMe devices.

        Returns:
            bool: True if NVMe devices are configured; False otherwise

        """
        return self.storage_class.value == "nvme"

    def override_params(self, data):
        """Override the values of the daos server yaml config file with the external data.

        Args:
            data (dict): external server configuration data.
        """
        self.log.info("Overriding storage tier %s config with external data", self._tier)
        for name in self.get_param_names():
            param = getattr(self, name)
            name = param.yaml_key or name
            if name in data:
                param.update(data[name], name)

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            StorageTierYamlParameters: a new StorageTierYamlParameters object
        """
        return StorageTierYamlParameters(self._base_namespace, self._tier)
