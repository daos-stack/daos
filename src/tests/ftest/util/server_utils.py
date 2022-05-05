#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from getpass import getuser
import math
import os
import socket
import time
import yaml

from avocado import fail_on

from command_utils_base import CommonConfig, BasicParameter
from exception_utils import CommandFailure
from command_utils import SubprocessManager
from general_utils import pcmd, get_log_file, human_to_bytes, bytes_to_human, \
    convert_list, get_default_config_file, distribute_files, DaosTestError, \
    stop_processes, get_display_size, run_pcmd, get_primary_group
from dmg_utils import get_dmg_command
from server_utils_base import \
    ServerFailed, DaosServerCommand, DaosServerInformation, AutosizeCancel
from server_utils_params import \
    DaosServerTransportCredentials, DaosServerYamlParameters
from ClusterShell.NodeSet import NodeSet


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
                 svr_config_temp=None, dmg_config_temp=None, manager="Orterun",
                 namespace="/run/server_manager/*"):
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
        """
        self.group = group
        server_command = get_server_command(
            group, svr_cert_dir, bin_dir, svr_config_file, svr_config_temp)
        super().__init__(server_command, manager, namespace)
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
        self.information = DaosServerInformation(self.dmg)

        # Flag used to determine which method is used to detect that the server has started
        self.detect_start_via_dmg = False

        # Parameters to set storage prepare and format timeout
        self.storage_prepare_timeout = BasicParameter(None, 40)
        self.storage_format_timeout = BasicParameter(None, 40)

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
        results = run_pcmd(self._hosts, str(cmd), timeout=self.storage_prepare_timeout.value)

        # gratuitously lifted from pcmd() and get_current_state()
        result = {}
        stdouts = ""
        for res in results:
            stdouts += '\n'.join(res["stdout"] + [''])
            if res["exit_status"] not in result:
                result[res["exit_status"]] = NodeSet()
            result[res["exit_status"]].add(res["hosts"])

        if len(result) > 1 or 0 not in result or \
            (using_dcpm and \
             "No SCM modules detected; skipping operation" in stdouts):
            dev_type = "nvme"
            if using_dcpm and using_nvme:
                dev_type = "dcpm & nvme"
            elif using_dcpm:
                dev_type = "dcpm"
            pcmd(self._hosts, "sudo -n ipmctl show -v -dimm")
            pcmd(self._hosts, "ndctl list ")
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

        # Search for patterns in the dmg system query output:
        #   - the expected number of pattern matches are detected (success)
        #   - the time out is reached (failure)
        #   - the subprocess is no longer running (failure)
        while not complete and not timed_out and sub_process.poll() is None:
            detected = self.detect_engine_states(expected_states)
            complete = detected == self.manager.job.pattern_count
            timed_out = time.time() - start > self.manager.job.pattern_timeout.value
            if not complete and not timed_out:
                time.sleep(1)

        # Summarize results
        self.manager.job.report_subprocess_status(start, detected, complete, timed_out, sub_process)

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
                "sudo chown -R {}:{} {}".format(user, get_primary_group(user), " ".join(scm_mount)))

        if cmd_list:
            pcmd(self._hosts, "; ".join(cmd_list), verbose)

    def restart(self, hosts, wait=False):
        """Restart the specified servers after a stop.

           The servers must have been previously formatted and started.

        Args:
            hosts (list): List of servers to restart.
            wait (bool): Whether or not to wait until the servers
                         have joined.
        """
        orig_hosts = self.manager.hosts
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
        self.log.info(
            "<SERVER> Formatting hosts: <%s>", self.dmg.hostlist)
        # Temporarily increasing timeout to avoid CI errors until DAOS-5764 can
        # be further investigated.
        self.dmg.storage_format(timeout=self.storage_format_timeout.value)

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

    def kill(self):
        """Forcibly terminate any server process running on hosts."""
        regex = self.manager.job.command_regex
        # Try to dump all server's ULTs stacks before kill.
        result = stop_processes(self._hosts, regex, dump_ult_stacks=True)
        if 0 in result and len(result) == 1:
            print(
                "No remote {} server processes killed (none found), "
                "done.".format(regex))
        else:
            print(
                "***At least one remote {} server process needed to be killed! "
                "Please investigate/report.***".format(regex))
        # set stopped servers state to make teardown happy
        self.update_expected_states(
            None, ["stopped", "excluded", "errored"])

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

    def update_config_file_from_file(self, dst_hosts, test_dir, generated_yaml):
        """Update config file and object.

        Create and place the new config file in /etc/daos/daos_server.yml
        Then update SCM-related data in engine_params so that those disks will
        be wiped.

        Args:
            dst_hosts (list): Destination server hostnames to place the new
                config file.
            test_dir (str): Directory where the server config data from
                generated_yaml will be written.
            generated_yaml (YAMLObject): New server config data.

        """
        # Create a temporary file in test_dir and write the generated config.
        temp_file_path = os.path.join(test_dir, "temp_server.yml")
        try:
            with open(temp_file_path, 'w') as write_file:
                yaml.dump(generated_yaml, write_file, default_flow_style=False)
        except Exception as error:
            raise CommandFailure(
                "Error writing the yaml file! {}: {}".format(
                    temp_file_path, error)) from error

        # Copy the config from temp dir to /etc/daos of the server node.
        default_server_config = get_default_config_file("server")
        try:
            distribute_files(
                dst_hosts, temp_file_path, default_server_config,
                verbose=False, sudo=True)
        except DaosTestError as error:
            raise CommandFailure(
                "ERROR: Copying yaml configuration file to {}: "
                "{}".format(dst_hosts, error)) from error

        # Before restarting daos_server, we need to clear SCM. Unmount the mount
        # point, wipefs the disks, etc. This clearing step is built into the
        # server start steps. It'll look at the engine_params of the
        # server_manager and clear the SCM set there, so we need to overwrite it
        # before starting to the values from the generated config.
        self.log.info("Resetting engine_params")
        self.manager.job.yaml.engine_params = []
        engines = generated_yaml["engines"]
        for i, engine in enumerate(engines):
            self.log.info("engine %d", i)
            for storage_tier in engine["storage"]:
                if storage_tier["class"] != "dcpm":
                    continue

                self.log.info("scm_mount = %s", storage_tier["scm_mount"])
                self.log.info("class = %s", storage_tier["class"])
                self.log.info("scm_list = %s", storage_tier["scm_list"])

                per_engine_yaml_parameters =\
                    DaosServerYamlParameters.PerEngineYamlParameters(i)
                per_engine_yaml_parameters.scm_mount.update(storage_tier["scm_mount"])
                per_engine_yaml_parameters.scm_class.update(storage_tier["class"])
                per_engine_yaml_parameters.scm_size.update(None)
                per_engine_yaml_parameters.scm_list.update(storage_tier["scm_list"])
                per_engine_yaml_parameters.reset_yaml_data_updated()

                self.manager.job.yaml.engine_params.append(
                    per_engine_yaml_parameters)

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
        storage_capacity = self.information.get_storage_capacity(
            self.manager.job.engine_params)

        self.log.info("Largest storage size available per engine:")
        for key in sorted(storage_capacity):
            # Use the same storage size across all engines
            storage[key] = min(storage_capacity[key])
            self.log.info(
                "  %-4s:  %s", key.upper(), get_display_size(storage[key]))
        return storage

    def autosize_pool_params(self, size, tier_ratio, scm_size, nvme_size,
                             min_targets=1, quantity=1):
        """Update any pool size parameter ending in a %.

        Use the current NVMe and SCM storage sizes to assign values to the size,
        scm_size, and or nvme_size dmg pool create arguments which end in "%".
        The numerical part of these arguments will be used to assign a value
        that is X% of the available storage capacity.  The updated size and
        nvme_size arguments will be assigned values that are multiples of 1GiB
        times the number of targets assigned to each server engine.  If needed
        the number of targets will be reduced (to not exceed min_targets) in
        order to support the requested size.  An optional number of expected
        pools (quantity) can also be specified to divide the available storage
        capacity.

        Note: depending upon the inputs this method may return dmg pool create
            parameter combinations that are not supported, e.g. tier_ratio +
            nvme_size.  This is intended to allow testing of these combinations.

        Args:
            size (object): the str, int, or None value for the dmp pool create
                size parameter.
            tier_ratio (object): the int or None value for the dmp pool create
                size parameter.
            scm_size (object): the str, int, or None value for the dmp pool
                create scm_size parameter.
            nvme_size (object): the str, int, or None value for the dmp pool
                create nvme_size parameter.
            min_targets (int, optional): the minimum number of targets per
                engine that can be configured. Defaults to 1.
            quantity (int, optional): Number of pools to account for in the size
                calculations. The pool size returned is only for a single pool.
                Defaults to 1.

        Raises:
            ServerFailed: if there was a error obtaining auto-sized TestPool
                parameters.
            AutosizeCancel: if a valid pool parameter size could not be obtained

        Returns:
            dict: the parameters for a TestPool object.

        """
        # Adjust any pool size parameter by the requested percentage
        params = {"tier_ratio": tier_ratio}
        adjusted = {"size": size, "scm_size": scm_size, "nvme_size": nvme_size}
        keys = [
            key for key in ("size", "scm_size", "nvme_size")
            if adjusted[key] is not None and str(adjusted[key]).endswith("%")]
        if keys:
            # Verify the minimum number of targets configured per engine
            targets = min(self.manager.job.get_engine_values("targets"))
            if targets < min_targets:
                raise ServerFailed(
                    "Minimum target quantity ({}) exceeds current target "
                    "quantity ({})".format(min_targets, targets))

            self.log.info("-" * 100)
            pool_msg = "{} pool{}".format(quantity, "s" if quantity > 1 else "")
            self.log.info(
                "Autosizing TestPool parameters ending with a \"%%\" for %s:",
                pool_msg)
            for key in ("size", "scm_size", "nvme_size"):
                self.log.info(
                    "  - %-9s : %s (%s)", key, adjusted[key], key in keys)

            # Determine the largest SCM and NVMe pool sizes can be used with
            # this server configuration with an optionally applied ratio.
            try:
                available_storage = self.get_available_storage()
            except ServerFailed as error:
                raise ServerFailed(
                    "Error obtaining available storage") from error

            # Determine the SCM and NVMe size limits for the size and tier_ratio
            # arguments for the total number of engines
            if tier_ratio is None:
                # Use the default value if not provided
                tier_ratio = 6
            engine_qty = len(self.manager.job.engine_params) * len(self._hosts)
            available_storage["size"] = min(
                engine_qty * available_storage["nvme"],
                (engine_qty * available_storage["scm"]) / float(tier_ratio / 100)
            )
            available_storage["tier_ratio"] = \
                available_storage["size"] * float(tier_ratio / 100)
            self.log.info(
                "Largest storage size available for %s engines with a %.2f%% "
                "tier_ratio:", engine_qty, tier_ratio)
            self.log.info(
                "  - NVME     : %s",
                get_display_size(available_storage["size"]))
            self.log.info(
                "  - SCM      : %s",
                get_display_size(available_storage["tier_ratio"]))
            self.log.info(
                "  - COMBINED : %s",
                get_display_size(
                    available_storage["size"] + available_storage["tier_ratio"]))

            # Apply any requested percentages to the pool parameters
            available = {
                "size": {"size": available_storage["size"], "type": "NVMe"},
                "scm_size": {"size": available_storage["scm"], "type": "SCM"},
                "nvme_size": {"size": available_storage["nvme"], "type": "NVMe"}
            }
            self.log.info("Adjusted pool sizes for %s:", pool_msg)
            for key in keys:
                try:
                    ratio = int(str(adjusted[key]).replace("%", ""))
                except NameError as error:
                    raise ServerFailed(
                        "Invalid '{}' format: {}".format(
                            key, adjusted[key])) from error
                adjusted[key] = \
                    (available[key]["size"] * float(ratio / 100)) / quantity
                self.log.info(
                    "  - %-9s : %-4s storage adjusted by %.2f%%: %s",
                    key, available[key]["type"], ratio,
                    get_display_size(adjusted[key]))

            # Display the pool size increment value for each size argument
            increment = {
                "size": human_to_bytes("1GiB"),
                "scm_size": human_to_bytes("16MiB"),
                "nvme_size": human_to_bytes("1GiB")}
            self.log.info("Increment sizes per target:")
            for key in keys:
                self.log.info(
                    "  - %-9s : %s", key, get_display_size(increment[key]))

            # Adjust the size to use a SCM/NVMe target multiplier
            self.log.info("Pool sizes adjusted to fit by increment sizes:")
            adjusted_targets = targets
            for key in keys:
                multiplier = math.floor(adjusted[key] / increment[key])
                params[key] = multiplier * increment[key]
                self.log.info(
                    "  - %-9s : %s * %s = %s",
                    key, multiplier, increment[key],
                    get_display_size(params[key]))
                if multiplier < adjusted_targets:
                    adjusted_targets = multiplier
                    if adjusted_targets < min_targets:
                        raise AutosizeCancel(
                            "Unable to autosize the {} pool parameter due to "
                            "exceeding the minimum of {} targets: {}".format(
                                key, min_targets, adjusted_targets))
                if key == "size":
                    tier_ratio_size = params[key] * float(tier_ratio / 100)
                    self.log.info(
                        "  - %-9s : %.2f%% tier_ratio = %s",
                        key, tier_ratio, get_display_size(tier_ratio_size))
                    params[key] += tier_ratio_size
                    self.log.info(
                        "  - %-9s : NVMe + SCM = %s",
                        key, get_display_size(params[key]))
                params[key] = bytes_to_human(params[key], binary=True)

            # Reboot the servers if a reduced number of targets is required
            if adjusted_targets < targets:
                self.log.info(
                        "Updating targets per server engine: %s -> %s",
                        targets, adjusted_targets)
                self.set_config_value("targets", adjusted_targets)
                self.stop()
                self.start()

            self.log.info("-" * 100)

        return params
