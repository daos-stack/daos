"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

from collections import defaultdict
from getpass import getuser
import os
import re
import time
import random

from avocado import fail_on

from ClusterShell.NodeSet import NodeSet
from command_utils_base import CommonConfig, BasicParameter
from command_utils import SubprocessManager
from dmg_utils import get_dmg_command
from exception_utils import CommandFailure
from general_utils import pcmd, get_log_file, list_to_str, get_display_size, run_pcmd
from general_utils import get_default_config_file
from host_utils import get_local_host
from server_utils_base import ServerFailed, DaosServerCommand, DaosServerInformation
from server_utils_params import DaosServerTransportCredentials, DaosServerYamlParameters
from user_utils import get_chown_command
from run_utils import run_remote, stop_processes


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
    command = DaosServerCommand(bin_dir, config, None)
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
                 svr_config_temp=None, dmg_config_temp=None, manager="Orterun",
                 namespace="/run/server_manager/*", access_points_suffix=None):
        # pylint: disable=too-many-arguments
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
            namespace (str): yaml namespace (path to parameters)
            access_points_suffix (str, optional): Suffix to append to each access point name.
                Defaults to None.
        """
        self.group = group
        server_command = get_server_command(
            group, svr_cert_dir, bin_dir, svr_config_file, svr_config_temp)
        super().__init__(server_command, manager, namespace)
        self.manager.job.sub_command_override = "start"

        # Dmg command to access this group of servers which will be configured
        # to access the daos_servers when they are started
        self.dmg = get_dmg_command(
            group, dmg_cert_dir, bin_dir, dmg_config_file, dmg_config_temp, access_points_suffix)

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
                "stopping", "stopped", "excluded", "errored", "unresponsive", "unknown"],
            "errored": ["errored"],
        }

        # Storage and network information
        self.information = DaosServerInformation(self.dmg)

        # Flag used to determine which method is used to detect that the server has started
        self.detect_start_via_dmg = False

        # Parameters to set storage prepare and format timeout
        self.storage_prepare_timeout = BasicParameter(None, 40)
        self.storage_format_timeout = BasicParameter(None, 40)
        self.storage_reset_timeout = BasicParameter(None, 120)
        self.collect_log_timeout = BasicParameter(None, 120)

        # Optional external yaml data to use to create the server config file, bypassing the values
        # defined in the self.manager.job.yaml object.
        self._external_yaml_data = None

    @property
    def engines(self):
        """Get the total number of engines.

        Returns:
            int: total number of engines

        """
        return len(self.ranks.keys())

    @property
    def ranks(self):
        """Get the rank and host pairing for all of the engines.

        Returns:
            dict: rank key with host value

        """
        return {rank: value["host"] for rank, value in self._expected_states.items()}

    @property
    def management_service_hosts(self):
        """Get the hosts running the management service.

        Returns:
            NodeSet: the hosts running the management service

        """
        return NodeSet.fromlist(self.get_config_value('access_points'))

    @property
    def management_service_ranks(self):
        """Get the ranks running the management service.

        Returns:
            list: a list of ranks (int) running the management service

        """
        return self.get_host_ranks(self.management_service_hosts)

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

        It should be called independently when a test variant is using servers
        started by a previous test variant.

        Args:
            hosts (list, optional): dmg hostlist value. Defaults to None which
                results in using the 'access_points' host list.
        """
        self._prepare_dmg_certificates()
        self._prepare_dmg_hostlist(hosts)

    def _prepare_dmg_certificates(self):
        """Set up dmg certificates."""
        self.dmg.copy_certificates(get_log_file("daosCA/certs"), get_local_host())

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
        self.manager.job.temporary_file_hosts = self._hosts.copy()
        self.manager.job.create_yaml_file(self._external_yaml_data)
        self.manager.job.update_pattern_timeout()

        # Copy certificates
        self.manager.job.copy_certificates(get_log_file("daosCA/certs"), self._hosts)
        self._prepare_dmg_certificates()

        # Prepare dmg for running storage format on all server hosts
        self._prepare_dmg_hostlist(self._hosts)
        if not self.dmg.yaml:
            # If using a dmg config file, transport security was
            # already configured.
            self.dmg.insecure.update(self.get_config_value("allow_insecure"), "dmg.insecure")

        # Kill any daos servers running on the hosts
        self.manager.kill()

        # Clean up any files that exist on the hosts
        self.clean_files()

        if storage:
            # Prepare server storage
            if self.manager.job.using_nvme or self.manager.job.using_dcpm:
                if hasattr(self.manager, "mca"):
                    self.manager.mca.update({"plm_rsh_args": "-l root"}, "orterun.mca", True)

        # Verify the socket directory exists when using a non-systemctl manager
        self.verify_socket_directory(getuser())

    def clean_files(self, verbose=True):
        """Clean up the daos server files.

        Args:
            verbose (bool, optional): display clean commands. Defaults to True.
        """
        clean_commands = []
        for index, engine_params in enumerate(self.manager.job.yaml.engine_params):
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
                    self.log.info("Cleaning up the following device(s): %s.", ", ".join(scm_list))
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

        if self.manager.job.using_control_metadata:
            # Remove the contents (superblocks) of the control plane metadata path
            cmd = "sudo rm -fr {}/*".format(self.manager.job.control_metadata.path.value)
            if cmd not in clean_commands:
                clean_commands.append(cmd)

            if self.manager.job.control_metadata.device.value is not None:
                # Dismount the control plane metadata mount point
                cmd = "while sudo umount {}; do continue; done".format(
                    self.manager.job.control_metadata.device.value)
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
        # Use the configuration file settings if no overrides specified
        if using_dcpm is None:
            using_dcpm = self.manager.job.using_dcpm
        if using_nvme is None:
            using_nvme = self.manager.job.using_nvme

        if using_dcpm:
            # Prepare SCM storage
            if not self.scm_prepare(target_user=user, ignore_config=True).passed:
                raise ServerFailed("Error preparing dcpm storage")

        if using_nvme:
            # Prepare NVMe storage
            if not self.nvme_prepare(target_user=user, ignore_config=True).passed:
                raise ServerFailed("Error preparing nvme storage")

    def scm_prepare(self, **kwargs):
        """Run daos_server scm prepare on the server hosts.

        Args:
            kwargs (dict, optional): named arguments and their values to use with the
                DaosServerCommand.ScmSubCommand.PrepareSubCommand object

        Raises:
            RemoteCommandResult: a grouping of the command results from the same hosts with the same
                return status

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_command(("scm", "prepare"), **kwargs)
        self.log.info("Preparing DAOS server storage: %s", str(cmd))
        result = run_remote(
            self.log, self._hosts, cmd.with_exports, timeout=self.storage_prepare_timeout.value)
        if not result.passed:
            # Add some debug due to the failure
            run_remote(self.log, self._hosts, "sudo -n ipmctl show -v -dimm")
            run_remote(self.log, self._hosts, "ndctl list")
        return result

    def scm_reset(self, **kwargs):
        """Run daos_server scm reset on the server hosts.

        Args:
            kwargs (dict, optional): named arguments and their values to use with the
                DaosServerCommand.ScmSubCommand.ResetSubCommand object

        Raises:
            RemoteCommandResult: a grouping of the command results from the same hosts with the same
                return status

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_command(("scm", "reset"), **kwargs)
        self.log.info("Resetting DAOS server storage: %s", str(cmd))
        return run_remote(
            self.log, self._hosts, cmd.with_exports, timeout=self.storage_prepare_timeout.value)

    def nvme_prepare(self, **kwargs):
        """Run daos_server nvme prepare on the server hosts.

        Args:
            kwargs (dict, optional): named arguments and their values to use with the
                DaosServerCommand.NvmeSubCommand.PrepareSubCommand object

        Returns:
            RemoteCommandResult: a grouping of the command results from the same hosts with the same
                return status

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        self.log.info("Preparing DAOS server storage: %s", str(cmd))
        cmd.set_command(("nvme", "prepare"), **kwargs)
        return run_remote(
            self.log, self._hosts, cmd.with_exports, timeout=self.storage_prepare_timeout.value)

    def support_collect_log(self, **kwargs):
        """Run daos_server support collect-log on the server hosts.

        Args:
            kwargs (dict, optional): named arguments and their values to use with the
                DaosServerCommand.SupportSubCommand.CollectLogSubCommand object

        Returns:
            RemoteCommandResult: a grouping of the command results from the same hosts with the same
                return status

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        cmd.config.value = get_default_config_file("server")
        self.log.info("Support collect-log on servers: %s", str(cmd))
        cmd.set_command(("support", "collect-log"), **kwargs)
        return run_remote(
            self.log, self._hosts, cmd.with_exports, timeout=self.collect_log_timeout.value)

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
                "Failed to start servers before format: {}".format(error)) from error

    def detect_engine_start(self, hosts_qty=None):
        """Detect when all the engines have started.

        Args:
            hosts_qty (int): number of servers expected to have been started.

        Raises:
            ServerFailed: if there was an error starting the servers after formatting.

        """
        if hosts_qty is None:
            hosts_qty = len(self._hosts)

        if self.detect_start_via_dmg:
            self.log.info("<SERVER> Waiting for the daos_engine to start via dmg system query")
            self.manager.job.update_pattern("dmg", hosts_qty)
            started = self.get_detected_engine_count(self.manager.process)
        else:
            self.log.info("<SERVER> Waiting for the daos_engine to start")
            self.manager.job.update_pattern("normal", hosts_qty)
            started = self.manager.check_subprocess_status(self.manager.process)

        if not started:
            self.manager.kill()
            raise ServerFailed("Failed to start servers after format")

        # Update the dmg command host list to work with pool create/destroy
        self._prepare_dmg_hostlist()

        # Define the expected states for each rank
        self._expected_states = self.get_current_state()

    def get_detected_engine_count(self, sub_process):
        """Get the number of detected joined engines.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command

        Returns:
            int: number of patterns detected in the job output

        """
        expected_states = self.manager.job.pattern.split(",")
        detected = 0
        complete = False
        timed_out = False
        start = time.time()
        elapsed = 0.0

        # Search for patterns in the dmg system query output:
        #   - the expected number of pattern matches are detected (success)
        #   - the time out is reached (failure)
        #   - the subprocess is no longer running (failure)
        while not complete and not timed_out \
                and (sub_process is None or sub_process.poll() is None):
            detected = self.detect_engine_states(expected_states)
            complete = detected == self.manager.job.pattern_count
            elapsed = time.time() - start
            timed_out = elapsed > self.manager.job.pattern_timeout.value
            if not complete and not timed_out:
                time.sleep(1)

        # Summarize results
        self.manager.job.report_subprocess_status(
            elapsed, detected, complete, timed_out, sub_process)

        return complete

    def detect_engine_states(self, expected_states):
        """Detect the number of engine states that match the expected states.

        Args:
            expected_states (list): a list of engine state strings to detect

        Returns:
            int: number of engine states that match the expected states

        """
        detected = 0
        states = self.get_current_state()
        for rank in sorted(states):
            if states[rank]["state"].lower() in expected_states:
                detected += 1
        return detected

    def reset_storage(self):
        """Reset the server storage.

        Raises:
            ServerFailed: if there was an error resetting the storage

        """
        cmd = DaosServerCommand(self.manager.job.command_path)
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_sub_command("nvme")
        cmd.sub_command_class.set_sub_command("reset")
        cmd.sub_command_class.sub_command_class.ignore_config.value = True

        self.log.info("Resetting DAOS server storage: %s", str(cmd))
        result = run_remote(
            self.log, self._hosts, cmd.with_exports, timeout=self.storage_reset_timeout.value)
        if not result.passed:
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
            scm_mount = engine_params.get_value("scm_mount")

            # Support single or multiple scm_mount points
            if not isinstance(scm_mount, list):
                scm_mount = [scm_mount]

            self.log.info("Changing ownership to %s for: %s", user, scm_mount)
            cmd_list.add(
                "sudo -n {}".format(get_chown_command(user, options="-R", file=" ".join(scm_mount)))
            )

        if cmd_list:
            pcmd(self._hosts, "; ".join(cmd_list), verbose)

    def restart(self, hosts, wait=False):
        """Restart the specified servers after a stop.

           The servers must have been previously formatted and started.

        Args:
            hosts (NodeSet): hosts on which to restart the servers.
            wait (bool, optional): Whether or not to wait until the servers have joined. Defaults to
                False.
        """
        orig_hosts = self.manager.hosts.copy()
        self.manager.assign_hosts(hosts)
        orig_pattern = self.manager.job.pattern
        orig_count = self.manager.job.pattern_count
        self.manager.job.update_pattern("normal", len(hosts))
        try:
            self.manager.run()

            host_ranks = self.get_host_ranks(hosts)
            self.update_expected_states(host_ranks, ["joined"])

            if not wait:
                return

            # Loop until we get the expected states or the test times out.
            while True:
                status = self.verify_expected_states(show_logs=False)
                if status["expected"]:
                    break
                time.sleep(1)
        finally:
            self.manager.assign_hosts(orig_hosts)
            self.manager.job.update_pattern(orig_pattern, orig_count)

    def start(self):
        """Start the server through the job manager."""
        # Prepare the servers
        self.prepare()

        # Start the servers and wait for them to be ready for storage format
        self.detect_format_ready()

        # Collect storage and network information from the servers.
        self.information.collect_storage_information()
        self.information.collect_network_information()

        # Format storage and wait for server to change ownership
        self.log.info("<SERVER> Formatting hosts: <%s>", self.dmg.hostlist)
        # Temporarily increasing timeout to avoid CI errors until DAOS-5764 can
        # be further investigated.
        self.dmg.storage_format(timeout=self.storage_format_timeout.value)

        # Wait for all the engines to start
        self.detect_engine_start()

        return True

    def stop(self):
        """Stop the server through the runner."""
        self.log.info("<SERVER> Stopping server %s command", self.manager.command)

        # Maintain a running list of errors detected trying to stop
        messages = []

        # Stop the subprocess running the job manager command
        try:
            super().stop()
        except CommandFailure as error:
            messages.append(
                "Error stopping the {} subprocess: {}".format(self.manager.command, error))

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
            raise ServerFailed("Failed to stop servers:\n  {}".format("\n  ".join(messages)))

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
            raise ServerFailed("Error obtaining {} output: {}".format(self.dmg, data))
        try:
            states = {rank["state"] for rank in data.values()}
        except KeyError as error:
            raise ServerFailed(
                "Unexpected result from {} - missing 'state' key: {}".format(
                    self.dmg, data)) from error
        if len(states) > 1:
            # Multiple states for different ranks detected
            raise ServerFailed("Multiple system states ({}) detected:\n  {}".format(states, data))
        return states.pop()

    def check_rank_state(self, ranks, valid_states, max_checks=1):
        """Check the states of list of ranks in DAOS system.

        Args:
            ranks(list): daos rank list whose state need's to be checked
            valid_states (list): list of expected states for the rank
            max_checks (int, optional): number of times to check the state
                Defaults to 1.
        Raises:
            ServerFailed: if there was error obtaining the data for daos
                          system query
        Returns:
            list: returns list of failed rank(s) if state does
                  not match the expected state, otherwise returns empty list.

        """
        checks = 0
        while checks < max_checks:
            if checks > 0:
                time.sleep(1)
            data = self.get_current_state()
            if not data:
                # The regex failed to get the rank and state
                raise ServerFailed("Error obtaining {} output: {}".format(self.dmg, data))
            checks += 1
            failed_ranks = []
            for rank in ranks:
                if data[rank]["state"] not in valid_states:
                    failed_ranks.append(rank)
            if not failed_ranks:
                return []

        return failed_ranks

    def check_system_state(self, valid_states, max_checks=1):
        """Check that the DAOS system state is one of the provided states.

        Fail the test if the current state does not match one of the specified
        valid states.  Optionally the state check can loop multiple times,
        sleeping one second between checks, by increasing the number of maximum
        checks.

        Args:
            valid_states (list): expected DAOS system states as a list of lowercase strings
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
            raise ServerFailed("Error starting DAOS:\n{}".format(self.dmg.result))

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
            raise ServerFailed("Error stopping DAOS:\n{}".format(self.dmg.result))

    def get_current_state(self):
        """Get the current state of the daos_server ranks.

        Returns:
            dict: dictionary of server rank keys, each referencing a dictionary
                of information containing at least the following information:
                    {"host": <>, "uuid": <>, "state": <>}
                This will be empty if there was error obtaining the dmg system query output.

        """
        data = {}
        try:
            query_data = self.dmg.system_query()
        except CommandFailure:
            query_data = {"status": 1}
        if query_data["status"] == 0:
            if "response" in query_data and "members" in query_data["response"]:
                if query_data["response"]["members"] is None:
                    return data
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
    def stop_ranks(self, ranks, daos_log, force=False, copy=False):
        """Kill/Stop the specific server ranks using this pool.

        Args:
            ranks (list): a list of daos server ranks (int) to kill
            daos_log (DaosLog): object for logging messages
            force (bool, optional): whether to use --force option to dmg system
                stop. Defaults to False.
            copy (bool, optional): Copy dmg command. Defaults to False.

        Raises:
            avocado.core.exceptions.TestFail: if there is an issue stopping the server ranks.

        """
        msg = "Stopping DAOS ranks {} from server group {}".format(
            ranks, self.get_config_value("name"))
        self.log.info(msg)
        daos_log.info(msg)

        # Stop desired ranks using dmg
        if copy:
            self.dmg.copy().system_stop(ranks=list_to_str(value=ranks), force=force)
        else:
            self.dmg.system_stop(ranks=list_to_str(value=ranks), force=force)

        # Update the expected status of the stopped/excluded ranks
        self.update_expected_states(ranks, ["stopped", "excluded"])

    def stop_random_rank(self, daos_log, force=False, exclude_ranks=None):
        """Kill/Stop a random server rank that is expected to be running.

        Args:
            daos_log (DaosLog): object for logging messages
            force (bool, optional): whether to use --force option to dmg system
                stop. Defaults to False.
            exclude_ranks (list, optional): ranks to exclude from the random selection.
                Default is None.

        Raises:
            avocado.core.exceptions.TestFail: if there is an issue stopping the server ranks.
            ServerFailed: if there are no available ranks to stop.

        """
        # Exclude non-running ranks
        rank_state = self.get_expected_states()
        candidate_ranks = []
        for rank, state in rank_state.items():
            for running_state in self._states["running"]:
                if running_state in state:
                    candidate_ranks.append(rank)
                    continue

        # Exclude specified ranks
        for rank in exclude_ranks or []:
            if rank in candidate_ranks:
                del candidate_ranks[candidate_ranks.index(rank)]

        if len(candidate_ranks) < 1:
            raise ServerFailed("No available candidate ranks to stop.")

        # Stop a random rank
        random_rank = random.choice(candidate_ranks)  # nosec
        return self.stop_ranks([random_rank], daos_log=daos_log, force=force)

    def start_ranks(self, ranks, daos_log):
        """Start the specific server ranks.

        Args:
            ranks (list): a list of daos server ranks to start
            daos_log (DaosLog): object for logging messages

        Raises:
            CommandFailure: if there is an issue running dmg system start

        Returns:
            dict: a dictionary of host ranks and their unique states.

        """
        msg = "Start DAOS ranks {} from server group {}".format(
            ranks, self.get_config_value("name"))
        self.log.info(msg)
        daos_log.info(msg)

        # Start desired ranks using dmg
        result = self.dmg.system_start(ranks=list_to_str(value=ranks))

        # Update the expected status of the started ranks
        self.update_expected_states(ranks, ["joined"])

        return result

    def kill(self):
        """Forcibly terminate any server process running on hosts."""
        regex = self.manager.job.command_regex
        detected, running = stop_processes(self.log, self._hosts, regex)
        if not detected:
            self.log.info(
                "No remote %s server processes killed on %s (none found), done.",
                regex, self._hosts)
        elif running:
            self.log.info(
                "***Unable to kill remote server %s process on %s! Please investigate/report.***",
                regex, running)
        else:
            self.log.info(
                "***At least one remote server %s process needed to be killed on %s! Please "
                "investigate/report.***", regex, detected)
        # set stopped servers state to make teardown happy
        self.update_expected_states(None, ["stopped", "excluded", "errored"])

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

    def update_config_file_from_file(self, generated_yaml):
        """Update config file and object.

        Use the specified data to generate and distribute the server configuration to the hosts.

        Also use this data to replace the engine storage configuration so that the storage options
        defined in the specified data are configured correctly as part of the server startup.

        Args:
            generated_yaml (YAMLObject): New server config data.
        """
        # Use the specified yaml data to create the server yaml file and copy it all the hosts
        self._external_yaml_data = generated_yaml

        # Before restarting daos_server, we need to clear SCM. Unmount the mount point, wipefs the
        # disks, etc. This clearing step is built into the server start steps. It'll look at the
        # engine_params of the server_manager and clear the SCM set there, so we need to overwrite
        # it before starting to the values from the generated config.
        self.manager.job.yaml.override_params(generated_yaml)

    def get_host_ranks(self, hosts):
        """Get the list of ranks for the specified hosts.

        Args:
            hosts (NodeSet): host from which to get ranks.

        Returns:
            list: a list of integer ranks matching the hosts provided

        """
        rank_list = []
        for rank, rank_state in self._expected_states.items():
            if rank_state["host"] in hosts:
                rank_list.append(rank)
        return rank_list

    def get_available_storage(self):
        """Get the largest available SCM and NVMe storage common to all servers.

        Raises:
            ServerFailed: if output from the dmg storage scan is missing or
                not in the expected format

        Returns:
            dict: a dictionary of the largest available storage common to all
                engines per storage type, "scm"s and "nvme", in bytes

        """
        storage = {}
        storage_capacity = self.information.get_storage_capacity(self.manager.job.engine_params)

        self.log.info("Largest storage size available per engine:")
        for key in sorted(storage_capacity):
            # Use the same storage size across all engines
            storage[key] = min(storage_capacity[key])
            self.log.info("  %-4s:  %s", key.upper(), get_display_size(storage[key]))
        return storage

    def get_daos_metrics(self, verbose=False, timeout=60):
        """Get daos_metrics for the server.

        Args:
            verbose (bool, optional): pass verbose to run_pcmd. Defaults to False.
            timeout (int, optional): pass timeout to each execution ofrun_pcmd. Defaults to 60.

        Returns:
            list: list of pcmd results for each host. See general_utils.run_pcmd for details.
                [
                    general_utils.run_pcmd(), # engine 0
                    general_utils.run_pcmd()  # engine 1
                ]

        """
        engines_per_host = self.get_config_value("engines_per_host") or 1
        engines = []
        daos_metrics_exe = os.path.join(self.manager.job.command_path, "daos_metrics")
        for engine in range(engines_per_host):
            results = run_pcmd(
                hosts=self._hosts, verbose=verbose, timeout=timeout,
                command="sudo {} -S {} --csv".format(daos_metrics_exe, engine))
            engines.append(results)
        return engines

    def get_host_log_files(self):
        """Get the active engine log file names on each host.

        Returns:
            dict: host keys with lists of log files on that host values

        """
        self.log.debug("Determining the current %s log files", self.manager.job.command)

        # Get a list of engine pids from all of the hosts
        host_engine_pids = defaultdict(list)
        result = run_remote(self.log, self.hosts, "pgrep daos_engine", False)
        for data in result.output:
            if data.passed:
                # Search each individual line of output independently to ensure a pid match
                for line in data.stdout:
                    match = re.findall(r'(^[0-9]+)', line)
                    for host in data.hosts:
                        host_engine_pids[host].extend(match)

        # Find the log files that match the engine pids on each host
        host_log_files = defaultdict(list)
        log_files = self.manager.job.get_engine_values("log_file")
        for host, pid_list in host_engine_pids.items():
            # Generate a list of all of the possible log files that could exist on this host
            file_search = []
            for log_file in log_files:
                for pid in pid_list:
                    file_search.append(".".join([log_file, pid]))
            # Determine which of those log files actually do exist on this host
            # This matches the engine pid to the engine log file name
            command = f"ls -1 {' '.join(file_search)} | grep -v 'No such file or directory'"
            result = run_remote(self.log, host, command, False)
            for data in result.output:
                for line in data.stdout:
                    match = re.findall(fr"^({'|'.join(file_search)})", line)
                    if match:
                        host_log_files[host].append(match[0])

        self.log.debug("Engine log files per host")
        for host in sorted(host_log_files):
            self.log.debug("  %s:", host)
            for log_file in sorted(host_log_files[host]):
                self.log.debug("    %s", log_file)

        return host_log_files

    def search_log(self, pattern):
        """Search the server log files on the remote hosts for the specified pattern.

        Args:
            pattern (str): the grep -E pattern to use to search the server log files

        Returns:
            int: number of patterns found

        """
        self.log.debug("Searching %s logs for '%s'", self.manager.job.command, pattern)
        host_log_files = self.get_host_log_files()

        # Search for the pattern in the remote log files
        matches = 0
        for host, log_files in host_log_files.items():
            log_file_matches = 0
            self.log.debug("Searching for '%s' in %s on %s", pattern, log_files, host)
            result = run_remote(self.log, host, f"grep -E '{pattern}' {' '.join(log_files)}")
            for data in result.output:
                if data.returncode == 0:
                    matches = re.findall(fr'{pattern}', '\n'.join(data.stdout))
                    log_file_matches += len(matches)
            self.log.debug("Found %s matches on %s", log_file_matches, host)
            matches += log_file_matches
        self.log.debug(
            "Found %s total matches for '%s' in the %s logs",
            matches, pattern, self.manager.job.command)
        return matches
