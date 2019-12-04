#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from logging import getLogger
from getpass import getuser
import os
from re import findall
from socket import gethostname

from avocado import fail_on
from avocado.utils import process
from ClusterShell.NodeSet import NodeSet, NodeSetParseError

from agent_utils import AgentManager, AgentCommand, AgentYamlParameters
from agent_utils import stop_agent_processes
from command_utils import ObjectWithParameters, BasicParameter, YamlParameters
from command_utils import CommandFailure, EnvironmentVariables
from logger_utils import TestLogger
from server_utils import ServerManager, ServerCommand, ServerYamlParameters
from server_utils import stop_server_processes


def get_partition_hosts(partition):
    """Get a list of hosts in the specified slurm partition.

    Args:
        partition (str): name of the partition

    Returns:
        list: list of hosts in the specified partition

    """
    log = getLogger()
    hosts = []
    if partition is not None:
        # Get the partition name information
        cmd = "scontrol show partition {}".format(partition)
        try:
            result = process.run(cmd, shell=True, timeout=10)
        except process.CmdError as error:
            log.warning(
                "Unable to obtain hosts from the %s slurm "
                "partition: %s", partition, error)
            result = None

        if result:
            # Get the list of hosts from the partition information
            output = result.stdout
            try:
                hosts = list(NodeSet(findall(r"\s+Nodes=(.*)", output)[0]))
            except (NodeSetParseError, IndexError):
                log.warning(
                    "Unable to obtain hosts from the %s slurm partition "
                    "output: %s", partition, output)
    return hosts


def include_local_host(hosts):
    """Ensure the local host is included in the specified host list.

    Args:
        hosts (list): list of hosts

    Returns:
        list: list of hosts including the local host

    """
    local_host = gethostname().split('.', 1)[0]
    if hosts is None:
        hosts = [local_host]
    elif local_host not in hosts:
        hosts.append(local_host)
    return hosts


class TransportCredentials(YamlParameters):
    """Transport credentials listing certificates for secure communication."""

    def __init__(self):
        """Initialize a TransportConfig object."""
        super(TransportCredentials, self).__init__(
            "/run/common_config/transport_config/*", None, "transport_config")

        # Transport credential parameters:
        #   - allow_insecure: false|true
        #       Specify 'false' to bypass loading certificates and use insecure
        #       communications channnels
        #
        #   - ca_cert: <file>, e.g. ".daos/daosCA.crt"
        #       Custom CA Root certificate for generated certs
        #
        #   - cert: <file>, e.g. ".daos/daos_agent.crt"
        #       Agent certificate for use in TLS handshakes
        #
        #   - key: <file>, e.g. ".daos/daos_agent.key"
        #       Key portion of Server Certificate
        #
        #   - client_cert_dir: <str>, e.g. "".daos/clients"
        #       Location of client certificates [daos_server only]
        #
        #   - server_name: <str>, e.g. "daos_server"
        #       Name of server accodring to its certificate [daos_agent only]
        #
        self.allow_insecure = BasicParameter(True, True)
        self.ca_cert = BasicParameter(None)
        self.cert = BasicParameter(None)
        self.key = BasicParameter(None)
        self.client_cert_dir = BasicParameter(None)
        self.server_name = BasicParameter(None, "daos_server")

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        yaml_data = super(TransportCredentials, self).get_yaml_data()

        # Convert the boolean value into a string
        if self.title is not None:
            yaml_data[self.title]["allow_insecure"] = self.allow_insecure.value
        else:
            yaml_data["allow_insecure"] = self.allow_insecure.value

        return yaml_data


class AccessPoints(object):
    # pylint: disable=too-few-public-methods
    """Defines an object for storing access point data."""

    def __init__(self, port=10001):
        """Initialize a AccessPoints object.

        Args:
            port (int, optional): port number. Defaults to 10001.
        """
        self.hosts = []
        self.port = port

    def __str__(self):
        """Return a comma-separated list of <host>:<port>."""
        return ",".join(
            [":".join([host, str(self.port)]) for host in self.hosts])


class CommonConfig(YamlParameters):
    """Defines common daos_agent and daos_server configuration file parameters.

    Includes:
        - the daos system name (name)
        - a list of access point nodes (access_points)
        - the default port number (port)
    """

    def __init__(self):
        """Initialize a CommonConfig object."""
        super(CommonConfig, self).__init__(
            "/run/common_config/*", None, None, TransportCredentials())

        # Common configuration paramters
        #   - name: <str>, e.g. "daos_server"
        #       Name associated with the DAOS system.
        #
        #   - access_points: <list>, e.g.  ["hostname1:10001"]
        #       Hosts can be specified with or without port, default port below
        #       assumed if not specified. Defaults to the hostname of this node
        #       at port 10000 for local testing
        #
        #   - port: <int>, e.g. 10001
        #       Default port number with whith to bind the daos_server. This
        #       will also be used when connecting to access points if the list
        #       only contains host names.
        #
        self.name = BasicParameter(None, "daos_server")
        self.access_points = AccessPoints(10001)
        self.port = BasicParameter(None, 10001)

    def update_hosts(self, hosts):
        """Update the list of hosts for the access point.

        Args:
            hosts (list): list of access point hosts
        """
        if isinstance(hosts, list):
            self.access_points.hosts = [host for host in hosts]
        else:
            self.access_points.hosts = []

    def get_params(self, test):
        """Get values for starting agents and server from the yaml file.

        Obtain the lists of hosts from the BasicParameter class attributes.

        Args:
            test (Test): avocado Test object
        """
        # Get the common parameters: name & port
        super(CommonConfig, self).get_params(test)
        self.access_points.port = self.port.value

        # Get the transport credentials parameters
        self.other_params.get_params(test)

    def get_yaml_data(self):
        """Convert the parameters into a dictionary to use to write a yaml file.

        Returns:
            dict: a dictionary of parameter name keys and values

        """
        yaml_data = super(CommonConfig, self).get_yaml_data()
        yaml_data.pop("pmix", None)
        if self.access_points.hosts:
            yaml_data["access_points"] = self.access_points.hosts
        return yaml_data


class HostManager(ObjectWithParameters):
    """Manage hosts for running daos_agent and daos_server."""

    def __init__(self, daos_log=None):
        """Initialize a HostManager object."""
        super(HostManager, self).__init__("/run/hosts/*")

        # Loggers
        self.log = getLogger()
        self.test_log = TestLogger(self.log, daos_log)

        # Host parameters
        #   test_servers        = list of hosts to run daos_server if a
        #                           server_partition is not specified
        #   test_clients        = list of hosts to run daos_agent if a
        #                           client_partition is not specified
        #   server_partition    = slurm partition containing the hosts to run
        #                           doas_server if a test_servers list is not
        #                           specified
        #   client_partition    = slurm partition containing the hosts to run
        #                           doas_agent if a test_clients list is not
        #                           specified
        self.test_servers = BasicParameter(None)
        self.test_clients = BasicParameter(None)
        self.server_partition = BasicParameter(None)
        self.client_partition = BasicParameter(None)

        # Lists of managers for the daos_agent and daos_server processes
        self.agent_managers = []
        self.server_managers = []

        # The common yaml parameters shared between the agent and server
        self.common_config = CommonConfig()

        # Settings used to enable / disable PMIx mode
        self.pmix = BasicParameter(None, True)      # Set to False for PMIx-less
        self.env = EnvironmentVariables()

    def get_params(self, test):
        """Get values for starting agents and server from the yaml file.

        Obtain the lists of hosts from the BasicParameter class attributes.

        Args:
            test (Test): avocado Test object
        """
        # Get the hosts for the agents and servers
        super(HostManager, self).get_params(test)

        # Use the partition names to set the host lists if the list is empty
        for name in ("server", "client"):
            hosts = getattr(self, "".join(["test_", name, "s"]))
            partition = getattr(self, "_".join([name, "partition"]))
            if partition.value is not None and hosts.value is None:
                hosts.value = get_partition_hosts(partition.value)
            elif partition.value is not None and hosts.value is not None:
                test.fail(
                    "Specifying both a {} partition name and a list of hosts "
                    "is not supported!".format(name))

        # For API calls include running the agent on the local host
        self.test_clients.value = include_local_host(self.test_clients.value)

        # Verify at least one server has been specified
        self.log.info("test_servers:  %s", self.test_servers.value)
        self.log.info("test_clients:  %s", self.test_clients.value)
        if not self.test_servers.value:
            test.fail("No servers specified for this test!")

        # Get the common agent and server yaml parameters
        self.common_config.get_params(test)

        # Update PMIx settings
        self._update_pmix_settings(test)

    def _update_pmix_settings(self, test):
        """Update environment vairables and access points for PMIx setting.

        Args:
            test (Test): avocado Test object
        """
        if self.pmix.value:
            # Add environment variable assignments
            self.env["CRT_ATTACH_INFO_PATH"] = test.tmp
            self.env["DAOS_SINGLETON_CLI"] = 1

            # Remove access points
            self.common_config.access_points.hosts = []

        else:
            # Remove environment variable assignments
            self.env["CRT_ATTACH_INFO_PATH"] = "\"\""
            self.env["DAOS_SINGLETON_CLI"] = 0

            # Add access points
            self.common_config.access_points.hosts = [
                host for host in self.test_servers.value[:1]]

    @property
    def hostlist_servers(self):
        """Get the list of hosts assigned with the server role."""
        return self.test_servers.value

    @property
    def hostlist_clients(self):
        """Get the list of hosts assigned with the client/agent role."""
        return self.test_clients.value

    def get_agent_config_value(self, name, index=0):
        """Get the value of the daos agent yaml configuration parameter name.

        Args:
            name (str): name of the yaml configuration parameter from which to
                get the value
            index (int, optional): agent manager index. Defaults to 0.

        Returns:
            object: the yaml configuration parameter value or None

        """
        try:
            return self.agent_managers[index].get_config_value(name)
        except IndexError:
            self.log.error("Invalid agent manager index: %s", index)
            return None

    def get_server_config_value(self, name, index=0):
        """Get the value of the daos server yaml configuration parameter name.

        Args:
            name (str): name of the yaml configuration parameter from which to
                get the value
            index (int, optional): server manager index. Defaults to 0.

        Returns:
            object: the yaml configuration parameter value or None

        """
        try:
            return self.server_managers[index].get_config_value(name)
        except IndexError:
            self.log.error("Invalid server manager index: %s", index)
            return None

    def get_access_points(self, index=0):
        """Get the list of access points for one server manager.

        Args:
            index (int, optional): server manager index. Defaults to 0.

        Returns:
            AccessPoints: an AccessPoints object or None if the index is invalid

        """
        try:
            return self.server_managers[index].job.yaml.access_points
        except IndexError:
            self.log.error("Invalid server manager index: %s", index)
            return None

    def prepare(self):
        """Prepare the hosts for running daos agents and servers.

        Remove any daos agent or server processes running on the hosts being
        used for testing.
        """
        stop_agent_processes(self.test_clients.value)
        stop_server_processes(self.test_servers.value)

    def start_agents(self, test):
        """Start the daos_agent processes.

        Args:
            test (TestWithServers): apricot TestWithServers object
        """
        self.add_agent_manager(test)
        self.configure_manager(
            "agent",
            self.agent_managers[-1],
            test,
            self.test_clients.value,
            test.hostfile_clients_slots)
        self.start_agent_managers()

    def start_servers(self, test):
        """Start the daos_server processes.

        Args:
            test (TestWithServers): apricot TestWithServers object
        """
        self.add_server_manager(test)
        self.configure_manager(
            "server",
            self.server_managers[-1],
            test,
            self.test_servers.value,
            test.hostfile_servers_slots)
        self.start_server_managers()

    def add_agent_manager(self, test, yaml_file_name=None, common_cfg=None,
                          timeout=60):
        """Add a new daos agent manager object to the agent manager list.

        When adding multiple agent managers unique yaml config file names and
        common config setting (due to the server group name) should be used.

        Args:
            test (TestWithServers): apricot TestWithServers object
            yaml_file_name (str, optional): daos agent config file name.
                Defaults to None, which will use a default file name.
            common_cfg (CommonConfig, optional): daos agent config file
                settings shared between the agent and server. Defaults to None,
                which uses the class CommonConfig.
            timeout (int, optional): number of seconds to wait for the daos
                agent to start before reporting an error. Defaults to 60.
        """
        self.log.info("--- ADDING AGENT MANAGER ---")
        if yaml_file_name is None:
            yaml_file_name = os.path.join(test.tmp, "test_daos_agent.yaml")
        if common_cfg is None:
            common_cfg = self.common_config
        agent_cfg = AgentYamlParameters(yaml_file_name, common_cfg)
        if test.agent_log is not None:
            self.log.info(
                "Using a test-specific daos_agent log file: %s",
                test.agent_log)
            agent_cfg.log_file.value = test.agent_log
        agent_cmd = AgentCommand(agent_cfg, test.bin, timeout)
        self.agent_managers.append(AgentManager(test.ompi_bin, agent_cmd))

    def add_server_manager(self, test, yaml_file_name=None, common_cfg=None,
                           timeout=60):
        """Add a new daos server manager object to the server manager list.

        When adding multiple server managers unique yaml config file names
        and common config setting (due to the server group name) should be used.

        Args:
            test (TestWithServers): apricot TestWithServers object
            yaml_file_name (str, optional): daos server config file name.
                Defaults to None, which will use a default file name.
            common_cfg (CommonConfig, optional): daos server config file
                settings shared between the agent and server. Defaults to None,
                which uses the class CommonConfig.
            timeout (int, optional): number of seconds to wait for the daos
                server to start before reporting an error. Defaults to 60.
        """
        self.log.info("--- ADDING SERVER MANAGER ---")
        if yaml_file_name is None:
            yaml_file_name = os.path.join(test.tmp, "test_daos_server.yaml")
        if common_cfg is None:
            common_cfg = self.common_config
        server_cfg = ServerYamlParameters(yaml_file_name, common_cfg)
        if test.server_log is not None:
            self.log.info(
                "Using a test-specific daos_server log file: %s",
                test.server_log)
            server_cfg.server_params[0].log_file.value = test.server_log
        server_cmd = ServerCommand(server_cfg, test.bin, timeout)
        self.server_managers.append(ServerManager(test.ompi_bin, server_cmd))

    def configure_manager(self, name, manager, test, hosts, slots):
        """Configure the agent/server manager object.

        Defines the environment variables, host list, and hostfile settings used
        to start the agent/server manager.

        Args:
            name (str): manager name
            manager (SubprocessManager): [description]
            test (TestWithServers): apricot TestWithServers object
            hosts (list): list of hosts on which to start the daos agent/server
            slots (int): number of slots per server to define in the hostfile
        """
        self.log.info("--- CONFIGURING %s MANAGER ---", name.upper())
        manager.get_params(test)
        manager.add_environment_variables(self.env)
        manager.add_environment_list(["PATH"])
        manager.hosts = (hosts, test.workdir, slots)

    @fail_on(CommandFailure)
    def start_agent_managers(self):
        """Start the daos_agent processes on each specified list of hosts."""
        self.log.info("--- STARTING AGENTS ---")
        self._start_manager_list("agent", self.agent_managers)

    @fail_on(CommandFailure)
    def start_server_managers(self):
        """Start the daos_server processes on each specified list of hosts."""
        self.log.info("--- STARTING SERVERS ---")
        self._start_manager_list("server", self.server_managers)

    def _start_manager_list(self, name, manager_list):
        """Start each manager in the specified list.

        Args:
            name (str): manager name
            manager_list (list): list of SubprocessManager objects to start
        """
        user = getuser()
        # We probalby want to do this parallel if end up with multiple managers
        for manager in manager_list:
            self.log.info(
                "Starting %s: group=%s, hosts=%s, config=%s",
                name, manager.get_config_value("name"), manager.hosts,
                manager.job.yaml.filename)
            manager.verify_socket_directory(user)
            manager.start()

    def stop_agents(self):
        """Stop the daos agents.

        Returns:
            list: a list of exceptions raised stopping the servers

        """
        self.test_log.info("Stopping agents")
        return self._stop_managers(self.agent_managers, "agents")

    def stop_servers(self):
        """Stop the daos server and I/O servers.

        Returns:
            list: a list of exceptions raised stopping the servers

        """
        self.test_log.info("Stopping servers")
        return self._stop_managers(self.server_managers, "servers")

    def _stop_managers(self, managers, name):
        """Stop each manager object in the specified list.

        Args:
            managers (list): list of managers to stop
            name (str): manager list name

        Returns:
            list: a list of exceptions raised stopping the managers

        """
        error_list = []
        if managers:
            for manager in managers:
                try:
                    manager.stop()
                except CommandFailure as error:
                    self.test_log.info("  {}".format(error))
                    error_list.append(
                        "Error stopping {}: {}".format(name, error))
        return error_list
