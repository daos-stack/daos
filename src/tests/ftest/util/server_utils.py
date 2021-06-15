#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from getpass import getuser
import math
import os
import socket
import time

from avocado import fail_on

from command_utils_base import CommandFailure, CommonConfig
from command_utils import SubprocessManager
from general_utils import pcmd, get_log_file, human_to_bytes, bytes_to_human, \
    convert_list, get_display_size
from dmg_utils import get_dmg_command
from server_utils_base import ServerFailed, DaosServerCommand
from server_utils_params import \
    DaosServerTransportCredentials, DaosServerYamlParameters


def get_server_command(group, cert_dir, bin_dir, config_file, config_temp=None):
    """Get the daos_server command object to manage.

    Args:
        group (str): daos_server group name
        cert_dir (str): directory in which to copy certificates
        bin_dir (str): location of the daos_server executable
        config_file (str): configuration file name and path
        config_temp (str, optional): file name and path to use to generate the
            configuration file locally and then copy it to all the hosts using
            the config_file specification. Defaults to None, which creates and
            utilizes the file specified by config_file.

    Returns:
        DaosServerCommand: the daos_server command object

    """
    transport_config = DaosServerTransportCredentials(cert_dir)
    common_config = CommonConfig(group, transport_config)
    config = DaosServerYamlParameters(config_file, common_config)
    command = DaosServerCommand(bin_dir, config)
    if config_temp:
        # Setup the DaosServerCommand to write the config file data to the
        # temporary file and then copy the file to all the hosts using the
        # assigned filename
        command.temporary_file = config_temp
    return command


class DaosServerManager(SubprocessManager):
    # pylint: disable=too-many-public-methods
    """Manages the daos_server execution on one or more hosts."""

    # Mapping of environment variable names to daos_server config param names
    ENVIRONMENT_VARIABLE_MAPPING = {
        "CRT_PHY_ADDR_STR": "provider",
        "OFI_INTERFACE": "fabric_iface",
        "OFI_PORT": "fabric_iface_port",
    }

    # Defined in telemetry_common.h
    D_TM_SHARED_MEMORY_KEY = 0x10242048

    def __init__(self, group, bin_dir,
                 svr_cert_dir, svr_config_file, dmg_cert_dir, dmg_config_file,
                 svr_config_temp=None, dmg_config_temp=None, manager="Orterun"):
        """Initialize a DaosServerManager object.

        Args:
            group (str): daos_server group name
            binary_dir (str): directory from which to run daos_server/dmg
            svr_cert_dir (str): directory in which to copy server certificates
            svr_config_file (str): daos_server configuration file name and path
            dmg_cert_dir (str): directory in which to copy dmg certificates
            dmg_config_file (str): dmg configuration file name and path
            svr_config_temp (str, optional): file name and path used to generate
                the daos_server configuration file locally and copy it to all
                the hosts using the config_file specification. Defaults to None.
            dmg_config_temp (str, optional): file name and path used to generate
                the dmg command configuration file locally and copy it to all
                the hosts using the config_file specification. Defaults to None.
            manager (str, optional): the name of the JobManager class used to
                manage the YamlCommand defined through the "job" attribute.
                Defaults to "Orterun".
        """
        server_command = get_server_command(
            group, svr_cert_dir, bin_dir, svr_config_file, svr_config_temp)
        super().__init__(server_command, manager)
        self.manager.job.sub_command_override = "start"

        # Dmg command to access this group of servers which will be configured
        # to access the daos_servers when they are started
        self.dmg = get_dmg_command(
            group, dmg_cert_dir, bin_dir, dmg_config_file, dmg_config_temp)

        # Set the correct certificate file ownership
        if manager == "Systemctl":
            self.manager.job.certificate_owner = "daos_server"
            self.dmg.certificate_owner = getuser()

        # Server states
        self._states = {
            "all": [
                "awaitformat", "starting", "ready", "joined", "stopping",
                "stopped", "excluded", "errored", "unresponsive", "unknown"],
            "running": ["ready", "joined"],
            "stopped": [
                "stopping", "stopped", "excluded", "errored", "unresponsive",
                "unknown"],
            "errored": ["errored"],
        }

        # Storage and network information
        self.information = {"storage": {}, "network": {}}

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Use the yaml file parameter values to assign the server command and
        orterun command parameters.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)
        # Get the values for the dmg parameters
        self.dmg.get_params(test)

    def prepare_dmg(self, hosts=None):
        """Prepare the dmg command prior to its execution.

        This is only required to be called once and is included as part of
        calling prepare() and start().

        It should be called idependently when a test variant is using servers
        started by a previous test variant.

        Args:
            hosts (list, optional): dmg hostlist value. Defaults to None which
                results in using the 'access_points' host list.
        """
        self._prepare_dmg_certificates()
        self._prepare_dmg_hostlist(hosts)

    def _prepare_dmg_certificates(self):
        """Set up dmg certificates."""
        local_host = socket.gethostname().split('.', 1)[0]
        self.dmg.copy_certificates(
            get_log_file("daosCA/certs"), local_host.split())

    def _prepare_dmg_hostlist(self, hosts=None):
        """Set up the dmg command host list to use the specified hosts.

        Args:
            hosts (list, optional): dmg hostlist value. Defaults to None which
                results in using the entire host list.
        """
        if hosts is None:
            hosts = self._hosts
        self.dmg.hostlist = hosts

    def prepare(self, storage=True):
        """Prepare to start daos_server.

        Args:
            storage (bool, optional): whether or not to prepare dcpm/nvme
                storage. Defaults to True.
        """
        self.log.info(
            "<SERVER> Preparing to start daos_server on %s with %s",
            self._hosts, self.manager.command)

        # Create the daos_server yaml file
        self.manager.job.temporary_file_hosts = self._hosts
        self.manager.job.create_yaml_file()

        # Copy certificates
        self.manager.job.copy_certificates(
            get_log_file("daosCA/certs"), self._hosts)
        self._prepare_dmg_certificates()

        # Prepare dmg for running storage format on all server hosts
        self._prepare_dmg_hostlist(self._hosts)
        if not self.dmg.yaml:
            # If using a dmg config file, transport security was
            # already configured.
            self.dmg.insecure.update(
                self.get_config_value("allow_insecure"), "dmg.insecure")

        # Kill any daos servers running on the hosts
        self.manager.kill()

        # Clean up any files that exist on the hosts
        self.clean_files()

        if storage:
            # Prepare server storage
            if self.manager.job.using_nvme or self.manager.job.using_dcpm:
                self.log.info("Preparing storage in <format> mode")
                self.prepare_storage("root")
                if hasattr(self.manager, "mca"):
                    self.manager.mca.update(
                        {"plm_rsh_args": "-l root"}, "orterun.mca", True)

        # Verify the socket directory exists when using a non-systemctl manager
        self.verify_socket_directory(getuser())

    def clean_files(self, verbose=True):
        """Clean up the daos server files.

        Args:
            verbose (bool, optional): display clean commands. Defaults to True.
        """
        clean_commands = []
        for index, engine_params in \
                enumerate(self.manager.job.yaml.engine_params):
            scm_mount = engine_params.get_value("scm_mount")
            self.log.info("Cleaning up the %s directory.", str(scm_mount))

            # Remove the superblocks
            cmd = "sudo rm -fr {}/*".format(scm_mount)
            if cmd not in clean_commands:
                clean_commands.append(cmd)

            # Remove the shared memory segment associated with this io server
            cmd = "sudo ipcrm -M {}".format(self.D_TM_SHARED_MEMORY_KEY + index)
            clean_commands.append(cmd)

            # Dismount the scm mount point
            cmd = "while sudo umount {}; do continue; done".format(scm_mount)
            if cmd not in clean_commands:
                clean_commands.append(cmd)

            if self.manager.job.using_dcpm:
                scm_list = engine_params.get_value("scm_list")
                if isinstance(scm_list, list):
                    self.log.info(
                        "Cleaning up the following device(s): %s.",
                        ", ".join(scm_list))
                    # Umount and wipefs the dcpm device
                    cmd_list = [
                        "for dev in {}".format(" ".join(scm_list)),
                        "do mount=$(lsblk $dev -n -o MOUNTPOINT)",
                        "if [ ! -z $mount ]",
                        "then while sudo umount $mount",
                        "do continue",
                        "done",
                        "fi",
                        "sudo wipefs -a $dev",
                        "done"
                    ]
                    cmd = "; ".join(cmd_list)
                    if cmd not in clean_commands:
                        clean_commands.append(cmd)

        pcmd(self._hosts, "; ".join(clean_commands), verbose)

    def prepare_storage(self, user, using_dcpm=None, using_nvme=None):
        """Prepare the server storage.

        Args:
            user (str): username
            using_dcpm (bool, optional): override option to prepare scm storage.
                Defaults to None, which uses the configuration file to determine
                if scm storage should be formatted.
            using_nvme (bool, optional): override option to prepare nvme
                storage. Defaults to None, which uses the configuration file to
                determine if nvme storage should be formatted.

        Raises:
            ServerFailed: if there was an error preparing the storage

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_sub_command("storage")
        cmd.sub_command_class.set_sub_command("prepare")
        cmd.sub_command_class.sub_command_class.target_user.value = user
        cmd.sub_command_class.sub_command_class.force.value = True

        # Use the configuration file settings if no overrides specified
        if using_dcpm is None:
            using_dcpm = self.manager.job.using_dcpm
        if using_nvme is None:
            using_nvme = self.manager.job.using_nvme

        if using_dcpm and not using_nvme:
            cmd.sub_command_class.sub_command_class.scm_only.value = True
        elif not using_dcpm and using_nvme:
            cmd.sub_command_class.sub_command_class.nvme_only.value = True

        if using_nvme:
            hugepages = self.get_config_value("nr_hugepages")
            cmd.sub_command_class.sub_command_class.hugepages.value = hugepages

        self.log.info("Preparing DAOS server storage: %s", str(cmd))
        result = pcmd(self._hosts, str(cmd), timeout=40)
        if len(result) > 1 or 0 not in result:
            dev_type = "nvme"
            if using_dcpm and using_nvme:
                dev_type = "dcpm & nvme"
            elif using_dcpm:
                dev_type = "dcpm"
            raise ServerFailed("Error preparing {} storage".format(dev_type))

    def detect_format_ready(self, reformat=False):
        """Detect when all the daos_servers are ready for storage format.

        Args:
            reformat (bool, optional): whether or detect reformat (True) or
                format (False) messages. Defaults to False.

        Raises:
            ServerFailed: if there was an error starting the servers.

        """
        f_type = "format" if not reformat else "reformat"
        self.log.info("<SERVER> Waiting for servers to be ready for %s", f_type)
        self.manager.job.update_pattern(f_type, len(self._hosts))
        try:
            self.manager.run()
        except CommandFailure as error:
            self.manager.kill()
            raise ServerFailed(
                "Failed to start servers before format: {}".format(
                    error)) from error

    def detect_engine_start(self, host_qty=None):
        """Detect when all the engines have started.

        Args:
            host_qty (int): number of servers expected to have been started.

        Raises:
            ServerFailed: if there was an error starting the servers after
                formatting.

        """
        if host_qty is None:
            hosts_qty = len(self._hosts)
        self.log.info("<SERVER> Waiting for the daos_engine to start")
        self.manager.job.update_pattern("normal", hosts_qty)
        if not self.manager.check_subprocess_status(self.manager.process):
            self.manager.kill()
            raise ServerFailed("Failed to start servers after format")

        # Update the dmg command host list to work with pool create/destroy
        self._prepare_dmg_hostlist()

        # Define the expected states for each rank
        self._expected_states = self.get_current_state()

    def reset_storage(self):
        """Reset the server storage.

        Raises:
            ServerFailed: if there was an error resetting the storage

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_sub_command("storage")
        cmd.sub_command_class.set_sub_command("prepare")
        cmd.sub_command_class.sub_command_class.nvme_only.value = True
        cmd.sub_command_class.sub_command_class.reset.value = True
        cmd.sub_command_class.sub_command_class.force.value = True

        self.log.info("Resetting DAOS server storage: %s", str(cmd))
        result = pcmd(self._hosts, str(cmd), timeout=120)
        if len(result) > 1 or 0 not in result:
            raise ServerFailed("Error resetting NVMe storage")

    def set_scm_mount_ownership(self, user=None, verbose=False):
        """Set the ownership to the specified user for each scm mount.

        Args:
            user (str, optional): user name. Defaults to None - current user.
            verbose (bool, optional): display commands. Defaults to False.

        """
        user = getuser() if user is None else user

        cmd_list = set()
        for engine_params in self.manager.job.yaml.engine_params:
            scm_mount = engine_params.scm_mount.value

            # Support single or multiple scm_mount points
            if not isinstance(scm_mount, list):
                scm_mount = [scm_mount]

            self.log.info("Changing ownership to %s for: %s", user, scm_mount)
            cmd_list.add(
                "sudo chown -R {0}:{0} {1}".format(user, " ".join(scm_mount)))

        if cmd_list:
            pcmd(self._hosts, "; ".join(cmd_list), verbose)

    def start(self):
        """Start the server through the job manager."""
        # Prepare the servers
        self.prepare()

        # Start the servers and wait for them to be ready for storage format
        self.detect_format_ready()

        # Collect storage information
        self.collect_information()

        # Format storage and wait for server to change ownership
        self.log.info(
            "<SERVER> Formatting hosts: <%s>", self.dmg.hostlist)
        # Temporarily increasing timeout to avoid CI errors until DAOS-5764 can
        # be further investigated.
        self.dmg.storage_format(timeout=40)

        # Wait for all the engines to start
        self.detect_engine_start()

        return True

    def stop(self):
        """Stop the server through the runner."""
        self.log.info(
            "<SERVER> Stopping server %s command", self.manager.command)

        # Maintain a running list of errors detected trying to stop
        messages = []

        # Stop the subprocess running the job manager command
        try:
            super().stop()
        except CommandFailure as error:
            messages.append(
                "Error stopping the {} subprocess: {}".format(
                    self.manager.command, error))

        # Kill any leftover processes that may not have been stopped correctly
        self.manager.kill()

        if self.manager.job.using_nvme:
            # Reset the storage
            try:
                self.reset_storage()
            except ServerFailed as error:
                messages.append(str(error))

            # Make sure the mount directory belongs to non-root user
            self.set_scm_mount_ownership()

        # Report any errors after all stop actions have been attempted
        if messages:
            raise ServerFailed(
                "Failed to stop servers:\n  {}".format("\n  ".join(messages)))

    def get_environment_value(self, name):
        """Get the server config value associated with the env variable name.

        Args:
            name (str): environment variable name for which to get a daos_server
                configuration value

        Raises:
            ServerFailed: Unable to find a daos_server configuration value for
                the specified environment variable name

        Returns:
            str: the daos_server configuration value for the specified
                environment variable name

        """
        try:
            setting = self.ENVIRONMENT_VARIABLE_MAPPING[name]

        except IndexError as error:
            raise ServerFailed(
                "Unknown server config setting mapping for the {} environment "
                "variable!".format(name)) from error

        return self.get_config_value(setting)

    def get_single_system_state(self):
        """Get the current homogeneous DAOS system state.

        Raises:
            ServerFailed: if a single state for all servers is not detected

        Returns:
            str: the current DAOS system state

        """
        data = self.get_current_state()
        if not data:
            # The regex failed to get the rank and state
            raise ServerFailed(
                "Error obtaining {} output: {}".format(self.dmg, data))
        try:
            states = list(set([data[rank]["state"] for rank in data]))
        except KeyError as error:
            raise ServerFailed(
                "Unexpected result from {} - missing 'state' key: {}".format(
                    self.dmg, data)) from error
        if len(states) > 1:
            # Multiple states for different ranks detected
            raise ServerFailed(
                "Multiple system states ({}) detected:\n  {}".format(
                    states, data))
        return states[0]

    def check_system_state(self, valid_states, max_checks=1):
        """Check that the DAOS system state is one of the provided states.

        Fail the test if the current state does not match one of the specified
        valid states.  Optionally the state check can loop multiple times,
        sleeping one second between checks, by increasing the number of maximum
        checks.

        Args:
            valid_states (list): expected DAOS system states as a list of
                lowercase strings
            max_checks (int, optional): number of times to check the state.
                Defaults to 1.

        Raises:
            ServerFailed: if there was an error detecting the server state or
                the detected state did not match one of the valid states

        Returns:
            str: the matching valid detected state

        """
        checks = 0
        daos_state = "????"
        while daos_state not in valid_states and checks < max_checks:
            if checks > 0:
                time.sleep(1)
            try:
                daos_state = self.get_single_system_state().lower()
            except ServerFailed as error:
                raise error
            checks += 1
            self.log.info("System state check (%s): %s", checks, daos_state)
        if daos_state not in valid_states:
            raise ServerFailed(
                "Error checking DAOS state, currently neither {} after "
                "{} state check(s)!".format(valid_states, checks))
        return daos_state

    def system_start(self):
        """Start the DAOS I/O Engines.

        Raises:
            ServerFailed: if there was an error starting the servers

        """
        self.log.info("Starting DAOS I/O Engines")
        self.check_system_state(("stopped"))
        self.dmg.system_start()
        if self.dmg.result.exit_status != 0:
            raise ServerFailed(
                "Error starting DAOS:\n{}".format(self.dmg.result))

    def system_stop(self, extra_states=None):
        """Stop the DAOS I/O Engines.

        Args:
            extra_states (list, optional): a list of DAOS system states in
                addition to "started" and "joined" that are verified prior to
                issuing the stop. Defaults to None.

        Raises:
            ServerFailed: if there was an error stopping the servers

        """
        valid_states = ["started", "joined"]
        if extra_states:
            valid_states.extend(extra_states)
        self.log.info("Stopping DAOS I/O Engines")
        self.check_system_state(valid_states)
        self.dmg.system_stop(force=True)
        if self.dmg.result.exit_status != 0:
            raise ServerFailed(
                "Error stopping DAOS:\n{}".format(self.dmg.result))

    def get_current_state(self):
        """Get the current state of the daos_server ranks.

        Returns:
            dict: dictionary of server rank keys, each referencing a dictionary
                of information containing at least the following information:
                    {"host": <>, "uuid": <>, "state": <>}
                This will be empty if there was error obtaining the dmg system
                query output.

        """
        data = {}
        try:
            query_data = self.dmg.system_query()
        except CommandFailure:
            query_data = {"status": 1}
        if query_data["status"] == 0:
            if "response" in query_data and "members" in query_data["response"]:
                for member in query_data["response"]["members"]:
                    host = member["fault_domain"].split(".")[0].replace("/", "")
                    if host in self._hosts:
                        data[member["rank"]] = {
                            "uuid": member["uuid"],
                            "host": host,
                            "state": member["state"],
                        }
        return data

    @fail_on(CommandFailure)
    def stop_ranks(self, ranks, daos_log, force=False):
        """Kill/Stop the specific server ranks using this pool.

        Args:
            ranks (list): a list of daos server ranks (int) to kill
            daos_log (DaosLog): object for logging messages
            force (bool, optional): whether to use --force option to dmg system
                stop. Defaults to False.

        Raises:
            avocado.core.exceptions.TestFail: if there is an issue stopping the
                server ranks.

        """
        msg = "Stopping DAOS ranks {} from server group {}".format(
            ranks, self.get_config_value("name"))
        self.log.info(msg)
        daos_log.info(msg)

        # Stop desired ranks using dmg
        self.dmg.system_stop(ranks=convert_list(value=ranks), force=force)

        # Update the expected status of the stopped/excluded ranks
        self.update_expected_states(ranks, ["stopped", "excluded"])

    def get_host(self, rank):
        """Get the host name that matches the specified rank.

        Args:
            rank (int): server rank number

        Returns:
            str: host name matching the specified rank

        """
        host = None
        if rank in self._expected_states:
            host = self._expected_states[rank]["host"]
        return host

    def get_host_ranks(self, hosts):
        """Get the list of ranks for the specified hosts.

        Args:
            hosts (list): a list of host names.

        Returns:
            list: a list of integer ranks matching the hosts provided

        """
        rank_list = []
        for rank in self._expected_states:
            if self._expected_states[rank]["host"] in hosts:
                rank_list.append(rank)
        return rank_list

    def collect_information(self):
        """Collect storage and network information from the servers."""
        self.collect_storage_information()
        self.collect_network_information()

    def collect_storage_information(self):
        """Collect storage information from the servers.

        Assigns the self.storage dictionary.
        """
        try:
            self.information["storage"] = self.dmg.storage_scan()
        except CommandFailure:
            self.information["storage"] = {}

    def collect_network_information(self):
        """Collect storage information from the servers.

        Assigns the self.network dictionary.
        """
        try:
            self.information["network"] = self.dmg.network_scan()
        except CommandFailure:
            self.information["network"] = {}

    def _check_information(self, key, section, retry=True):
        """Check that the information dictionary is populated with data.

        Args:
            key (str): the information section to verify, e.g. "storage" or
                "network"
            section (str): The "response" key/section to verify
            retry (bool): if set attempt to collect the server storage and
                network information and call the method again. Defaults to True.

        Raises:
            ServerFailed: if output from the dmg command is missing or not in
                the expected format

        """
        msg = ""
        if key not in self.information:
            msg = "Internal error: invalid information key: {}".format(key)
        elif not self.information[key]:
            self.log.info("Server storage/network information not collected")
            if retry:
                self.collect_information()
                self._check_information(key, section, retry=False)
        elif "status" not in self.information[key]:
            msg = "Missing information status - verify dmg command output"
        elif "response" not in self.information[key]:
            msg = "No dmg {} scan 'response': status={}".format(
                key, self.information[key]["status"])
        elif section not in self.information[key]["response"]:
            msg = "No '{}' entry found in information 'response': {}".format(
                section, self.information[key]["response"])
        if msg:
            self.log.error(msg)
            raise ServerFailed("ServerInformation: {}".format(msg))

    def get_numa_node_info(self):
        """Determine what NVMe and SCM storage is available per socket.

        Raises:
            ServerFailed: if output from the dmg storage scan is missing or
                not in the expected format

        Returns:
            dict: [description]

        """
        self._check_information("storage", "HostStorage")
        self._check_information("network", "HostFabrics")

        data = {}
        try:
            _info = self.information["storage"]["response"]["HostStorage"]
            for entry in _info.values():
                hosts = entry["hosts"].split(":")[0]
                data[hosts] = {}
                if entry["storage"]["nvme_devices"]:
                    data[hosts] = {"nvme": {}}
                    for device in entry["storage"]["nvme_devices"]:
                        data[hosts]["nvme"][device["pci_addr"]] = \
                            device["socket_id"]
                if entry["storage"]["scm_namespaces"]:
                    data[hosts] = {"scm": {}}
                    for device in entry["storage"]["scm_namespaces"]:
                        data[hosts]["scm"][device["blockdev"]] = \
                            device["numa_node"]
        except KeyError as error:
            raise ServerFailed(
                "ServerInformation: Error obtaining storage data") from error

        return data

    def get_interface_providers(self):
        """Determine what interfaces are available with each provider.

        Raises:
            ServerFailed: if output from the dmg network scan is missing or
                not in the expected format

        Returns:
            [type]: [description]

        """
        self._check_information("network", "HostFabrics")

        data = {}
        try:
            _info = self.information["network"]["response"]["HostFabrics"]
            for entry in _info.values():
                hosts = entry["HostSet"].split(":")[0]
                data[hosts] = {}
                if entry["HostFabric"]["Interfaces"]:
                    for device in entry["HostFabric"]["Interfaces"]:
                        if device["Device"] not in data[hosts]:
                            data[hosts][device["Device"]] = []
                        data[hosts][device["Device"]].append(device["Provider"])
        except KeyError as error:
            raise ServerFailed(
                "ServerInformation: Error obtaining network data") from error

        return data

    def get_storage_capacity(self):
        """Get the configured SCM and NVMe storage per server engine.

        Only sums up capacities of devices that have been specified in the
        server configuration file.

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
            _info = self.information["storage"]["response"]["HostStorage"]
            for entry in _info.values():
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
                    "  %s capacities for %s: %s",
                    category.upper(), device, sizes)

        # Determine what storage is currently configured for each engine
        storage_capacity = {"scm": [], "nvme": []}
        for engine_param in self.manager.job.engine_params:
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
            self.log.info("  %s engine capacities: %s", category.upper(), sizes)

        return storage_capacity

    def get_available_storage(self):
        """Get the largest available SCM and NVMe storage common to all servers.

        Raises:
            ServerFailed: if output from the dmg storage scan is missing or
                not in the expected format

        Returns:
            list: a list of the maximum available SCM size and NVMe sizes in
                bytes

        """
        storage = []
        storage_capacity = self.get_storage_capacity()

        self.log.info("Total available storage:")
        for key in ["scm", "nvme"]:
            if storage_capacity[key]:
                # Use the minimum storage across all engines
                storage.append(min(storage_capacity[key]))
                self.log.info(
                    "  %-4s:  %s", key.upper(), get_display_size(storage[-1]))
        return storage

    def get_min_pool_nvme_size(self, target_qty=None):
        """Get the smallest pool nvme_size supported by this server config.

        Args:
            target_qty: (int, optional): number of targets to use in determining
                the minimum supported nvme_size. Defaults to None which uses the
                smallest current engine configuration setting.

        Returns:
            int: nvme_size in bytes

        """
        if target_qty is None:
            target_list = self.manager.job.get_engine_values("targets")
            target_qty = min(target_list)
        return human_to_bytes("{}GiB".format(target_qty))

    def autosize_pool_params(self, scm_ratio=6, nvme_ratio=90, min_targets=1,
                             quantity=1):
        """Get a test pool object configured to fit the current server config.

        The TestPool parameters returned by this method will be configure a
        TestPool object with a size (SCM or SCM + NVMe) that does not exceed the
        specified requirements (scm_ratio, nvme_ratio, and the pool quantity):
            nvme_ratio > 0:
                - the pool is configured with both SCM and NVMe
                - the pool NVMe size equal to the largest 1GiB increment * the
                  number of targets per engine that does not exceed the
                  nvme_ratio of the smallest total NVMe capacity of each engine.
                - if needed the number of targets per engine will be reduced to
                  meet this size requirement, down to min_targets.
                - the pool SCM size will be defined by the scm_ratio - as a
                  precentage of the NVMe size
            nvme_ratio == 0:
                - the pool is configured with SCM only
                - the pool SCM size is set to the scm_ratio of the smallest
                  total SCM capacity of each engine

        Args:
            scm_ratio (int, optional): when creating a pool with NVMe
                (nvme_ratio > 0) this defines the pool SCM size as a percentage
                of the pool NVMe size. When creating a pool without NVMe
                (nvme_ratio == 0) this defines the pool SCM size as a percentage
                of the available SCM storage. Defaults to 6.
            nvme_ratio (int, optional): this defines the pool NVMe size as a
                percentage of the available NVMe storage, e.g. 80. If set to 0
                the pool is created with SCM only. Defaults to 90.
            min_targets (int, optional): the minimum number of targets per
                engine that can be configured. Defaults to 1.
            quantity (int, optional): Number of pools to account for in the size
                calculations. The pool size returned is only for a single pool.
                Defaults to 1.

        Raises:
            ServerFailed: if there was a problem obtaining auto-sized TestPool
                parameters.

        Returns:
            dict: the parameters for a TestPool object.

        """
        # Use the default scm_ratio if one is not provided
        if scm_ratio is None:
            scm_ratio = 6

        pool_msg = "{} pool{}".format(quantity, "s" if quantity > 1 else "")
        self.log.info(
            "Autosizing TestPool parameters for %s with a %s%% "
            "scm_ratio and a %s%% nvme_ratio:", pool_msg, scm_ratio, nvme_ratio)

        # Determine the largest SCM and NVMe pool sizes can be used with this
        # server configuration with an optionally applied ratio.
        try:
            available_storage = self.get_available_storage()
        except ServerFailed as error:
            raise ServerFailed(
                "Pool Autosizing: error obtaining available storage: {}".format(
                    error))

        pool_params = {
            "size": None,
            "scm_ratio": None,
            "scm_size": None,
            "nvme_size": None,
        }
        if nvme_ratio:
            # Apply the ratio to the available NVMe size per engine
            adjusted_nvme = available_storage[1] * float(nvme_ratio / 100)
            adjusted_nvme /= quantity
            self.log.info(
                "  - NVMe storage adjusted by %.2f%% for %s: %s",
                nvme_ratio, pool_msg, get_display_size(adjusted_nvme))

            # Determine the largest supported NVMe size based upon the SCM ratio
            scm_reduction = max(90, nvme_ratio)
            adjusted_scm = available_storage[0] * float(scm_reduction / 100)
            adjusted_scm /= quantity
            self.log.info(
                "  - SCM storage adjusted by %.2f%% for %s: %s",
                scm_reduction, pool_msg, get_display_size(adjusted_scm))
            max_nvme_scm = adjusted_scm / float(scm_ratio / 100)
            self.log.info(
                "  - Max NVMe size supported by %.2f%% of the available SCM "
                "for %s: %s",
                scm_ratio, pool_msg, get_display_size(max_nvme_scm))

            # The largest NVMe size supported is the smallest of the two limits
            max_nvme_size = min([max_nvme_scm, adjusted_nvme])
            self.log.info(
                    "  - Max NVMe size supported for %s: %s",
                    pool_msg, get_display_size(max_nvme_size))

            # Determine the minimum number of targets configured per engine
            current_targets = min(self.manager.job.get_engine_values("targets"))
            targets = current_targets
            while targets >= min_targets and pool_params["size"] is None:
                # The I/O Engine allocates NVMe storage on targets in multiples
                # of 1GiB per target.  Determine the smallest NVMe pool size
                # that is supported by the current number of targets.
                nvme_size = self.get_min_pool_nvme_size(targets)
                self.log.info(
                    "  - Minimum NVMe pool size with %s targets: %s",
                    targets, get_display_size(nvme_size))

                # If the current number of targets results in a pool NVMe size
                # that is too large, attempt the calculation with less targets
                if nvme_size > max_nvme_size:
                    self.log.info(
                        "  - NVMe pool size with %s targets is too large: %s",
                        targets, get_display_size(nvme_size))
                    targets -= 1
                    continue

                # Determine the largest NVMe size can be configured
                nvme_size *= math.floor(max_nvme_size / nvme_size)
                self.log.info(
                    "  - NVMe pool size with %s targets: %s",
                    targets, get_display_size(nvme_size))
                pool_params["size"] = bytes_to_human(nvme_size, binary=True)
                pool_params["scm_ratio"] = scm_ratio

            # Cancel the target count cannot be reduced far enough to meet the
            # ratio requirements
            if pool_params["size"] is None:
                raise ServerFailed(
                    "Pool Autosizing: a server target count lower than {} is "
                    "required to support a {}% scm_ratio and a {}% "
                    "nvme_ratio.".format(min_targets, scm_ratio, nvme_ratio))

            # Update the servers if a reduced target count is required
            if targets < current_targets:
                self.log.info(
                    "Updating server targets: %s -> %s",
                    current_targets, targets)
                self.set_config_value("targets", targets)
                self.stop()
                self.start()

        elif scm_ratio:
            # Apply the ratio to the available SCM size per engine
            scm_size = available_storage[0] * float(scm_ratio / 100)
            self.log.info(
                "  - SCM storage adjusted by %.2f%%: %s",
                scm_ratio, get_display_size(scm_size))
            # pool_params["scm_size"] = scm_size
            pool_params["scm_size"] = bytes_to_human(scm_size, binary=True)

        else:
            raise ServerFailed(
                "Pool Autosizing: invalid scm_ratio ({}) and nvme_ratio ({}) "
                "combination specified".format(scm_ratio, nvme_ratio))

        return pool_params
