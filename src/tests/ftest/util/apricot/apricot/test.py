#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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

# Some useful test classes inherited from avocado.Test

from __future__ import print_function

import os
import json
import re
from getpass import getuser

from avocado import Test as avocadoTest
from avocado import skip, TestFail, fail_on

import fault_config_utils
from pydaos.raw import DaosContext, DaosLog, DaosApiError
from command_utils_base import \
    CommandFailure, EnvironmentVariables, CommonConfig
from agent_utils_params import \
    DaosAgentTransportCredentials, DaosAgentYamlParameters
from agent_utils import DaosAgentCommand, DaosAgentManager, include_local_host
from server_utils_params import \
    DaosServerTransportCredentials, DaosServerYamlParameters
from dmg_utils_params import \
    DmgYamlParameters, DmgTransportCredentials
from dmg_utils import DmgCommand
from daos_utils import DaosCommand
from server_utils import DaosServerCommand, DaosServerManager
from general_utils import \
    get_partition_hosts, stop_processes, get_job_manager_class
from logger_utils import TestLogger
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from env_modules import load_mpi
from distutils.spawn import find_executable
from write_host_file import write_host_file


# pylint: disable=invalid-name
def skipForTicket(ticket):
    """Skip a test with a comment about a ticket."""
    return skip("Skipping until {} is fixed.".format(ticket))
# pylint: enable=invalid-name


def get_log_file(name):
    """Get the full log file name and path.

    Args:
        name (str): log file name

    Returns:
        str: full log file name including path

    """
    return os.path.join(os.environ.get("DAOS_TEST_LOG_DIR", "/tmp"), name)


class Test(avocadoTest):
    """Basic Test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super(Test, self).__init__(*args, **kwargs)

        # Support specifying timeout values with units, e.g. "1d 2h 3m 4s".
        # Any unit combination may be used, but they must be specified in
        # descending order. Spaces can optionally be used between units and
        # values. The first unit character is required; other unit characters
        # are optional. The units are case-insensitive.
        # The following units are supported:
        #   - days      e.g. 1d, 1 day
        #   - hours     e.g. 2h, 2 hrs, 2 hours
        #   - minutes   e.g. 3m, 3 mins, 3 minutes
        #   - seconds   e.g. 4s, 4 secs, 4 seconds
        if isinstance(self.timeout, str):
            pattern = r""
            for interval in ("days", "hours", "minutes", "seconds"):
                pattern += r"(?:(\d+)(?:\s*{0}[{1}]*\s*)){{0,1}}".format(
                    interval[0], interval[1:])
            # pylint: disable=no-member
            dhms = re.search(pattern, self.timeout, re.IGNORECASE).groups()
            # pylint: enable=no-member
            self.timeout = 0
            for index, multiplier in enumerate([24 * 60 * 60, 60 * 60, 60, 1]):
                if dhms[index] is not None:
                    self.timeout += multiplier * int(dhms[index])

        # param to add multiple timeouts for different tests under
        # same test class
        self.timeouts = self.params.get(self.get_test_name(),
                                        "/run/timeouts/*")
        # If not specified, set a default timeout of 1 minute.
        # Tests that require a longer timeout should set a "timeout: <int>"
        # entry in their yaml file.  All tests should have a timeout defined.
        if (not self.timeout) and (not self.timeouts):
            self.timeout = 60
        elif self.timeouts:
            self.timeout = self.timeouts
        self.log.info("self.timeout: %s", self.timeout)

        item_list = self.logdir.split('/')
        for index, item in enumerate(item_list):
            if item == 'job-results':
                self.job_id = item_list[index + 1]
                break

        self.log.info("Job-ID: %s", self.job_id)
        self.log.info("Test PID: %s", os.getpid())

    # pylint: disable=invalid-name
    def cancelForTicket(self, ticket):
        """Skip a test due to a ticket needing to be completed."""
        return self.cancel("Skipping until {} is fixed.".format(ticket))
    # pylint: enable=invalid-name

    def get_test_name(self):
        """Obtain the test method name from the Avocado test name.

        Returns:
            str: name of the test method

        """
        return (self.__str__().split(".", 4)[3]).split(";", 1)[0]


class TestWithoutServers(Test):
    """Run tests without DAOS servers.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super(TestWithoutServers, self).__init__(*args, **kwargs)

        self.client_mca = None
        self.orterun = None
        self.ofi_prefix = None
        self.ompi_prefix = None
        self.basepath = None
        self.prefix = None
        self.bin = None
        self.daos_test = None
        self.cart_prefix = None
        self.cart_bin = None
        self.tmp = None
        self.fault_file = None
        self.context = None
        self.d_log = None

        # Create a default TestLogger w/o a DaosLog object to prevent errors in
        # tearDown() if setUp() is not completed.  The DaosLog is added upon the
        # completion of setUp().
        self.test_log = TestLogger(self.log, None)

    def setUp(self):
        """Set up run before each test."""
        super(TestWithoutServers, self).setUp()

        load_mpi('openmpi')

        self.orterun = find_executable('orterun')
        if self.orterun is None:
            self.fail("Could not find orterun")

        # hardware tests segfault in MPI_Init without this option
        self.client_mca = "--mca btl_openib_warn_default_gid_prefix 0"
        self.client_mca += " --mca pml ob1"
        self.client_mca += " --mca btl tcp,self"
        self.client_mca += " --mca oob tcp"
        self.ompi_prefix = os.path.dirname(os.path.dirname(self.orterun))
        # get paths from the build_vars generated by build
        with open('../../.build_vars.json') as build_vars:
            build_paths = json.load(build_vars)
        self.basepath = os.path.normpath(os.path.join(build_paths['PREFIX'],
                                                      '..') + os.path.sep)
        self.prefix = build_paths['PREFIX']
        try:
            self.ofi_prefix = build_paths['OFI_PREFIX']
        except KeyError:
            self.ofi_prefix = "/usr"
        self.bin = os.path.join(self.prefix, 'bin')
        self.daos_test = os.path.join(self.prefix, 'bin', 'daos_test')

        # set default shared dir for daos tests in case DAOS_TEST_SHARED_DIR
        # is not set, for RPM env and non-RPM env.
        if self.prefix != "/usr":
            self.tmp = os.path.join(self.prefix, 'tmp')
        else:
            self.tmp = os.getenv(
                'DAOS_TEST_SHARED_DIR', os.path.expanduser('~/daos_test'))
        if not os.path.exists(self.tmp):
            os.makedirs(self.tmp)

        # setup fault injection, this MUST be before API setup
        fault_list = self.params.get("fault_list", '/run/faults/*')
        if fault_list:
            # not using workdir because the huge path was messing up
            # orterun or something, could re-evaluate this later
            self.fault_file = fault_config_utils.write_fault_file(self.tmp,
                                                                  fault_list,
                                                                  None)
            os.environ["D_FI_CONFIG"] = self.fault_file

        self.context = DaosContext(self.prefix + '/lib64/')
        self.d_log = DaosLog(self.context)
        self.test_log.daos_log = self.d_log

    def tearDown(self):
        """Tear down after each test case."""
        super(TestWithoutServers, self).tearDown()

        if self.fault_file:
            os.remove(self.fault_file)


class TestWithServers(TestWithoutServers):
    # pylint: disable=too-many-public-methods
    """Run tests with DAOS servers and at least one client.

    Optionally run DAOS clients on specified hosts.  By default run a single
    DAOS client on the host executing the test.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super(TestWithServers, self).__init__(*args, **kwargs)

        # Add additional time to the test timeout for reporting running
        # processes while stopping the daos_agent and daos_server.
        tear_down_timeout = 30
        self.timeout += tear_down_timeout
        self.log.info(
            "Increasing timeout by %s seconds for agent/server tear down: %s",
            tear_down_timeout, self.timeout)

        self.server_group = None
        self.agent_managers = []
        self.server_managers = []
        self.setup_start_servers = True
        self.setup_start_agents = True
        self.hostlist_servers = None
        self.hostlist_clients = None
        self.hostfile_clients = None
        self.server_partition = None
        self.client_partition = None
        self.hostfile_servers_slots = 1
        self.hostfile_clients_slots = 1
        self.pool = None
        self.container = None
        self.agent_log = None
        self.server_log = None
        self.control_log = None
        self.helper_log = None
        self.client_log = None
        self.config_file_base = "test"
        self.log_dir = os.path.split(
            os.getenv("D_LOG_FILE", "/tmp/server.log"))[0]
        self.test_id = "{}-{}".format(
            os.path.split(self.filename)[1], self.name.str_uid)
        # self.debug = False
        # self.config = None
        self.job_manager = None

    def setUp(self):
        """Set up each test case."""
        super(TestWithServers, self).setUp()

        # Support configuring the startup of servers and agents by the setup()
        # method from the test yaml file
        self.setup_start_servers = self.params.get(
            "start_servers", "/run/setup/*", self.setup_start_servers)
        self.setup_start_agents = self.params.get(
            "start_agents", "/run/setup/*", self.setup_start_agents)

        # The server config name should be obtained from each ServerManager
        # object, but some tests still use this TestWithServers attribute.
        self.server_group = self.params.get(
            "name", "/server_config/", "daos_server")

        # Support using different job managers to launch the daos agent/servers
        self.manager_class = self.params.get("manager_class", "/", "Orterun")

        # Determine which hosts to use as servers and optionally clients.
        self.hostlist_servers = self.params.get("test_servers", "/run/hosts/*")
        self.hostlist_clients = self.params.get("test_clients", "/run/hosts/*")

        # If server or client host list are defined through valid slurm
        # partition names override any hosts specified through lists.
        for name in ("servers", "clients"):
            host_list_name = "_".join(["hostlist", name])
            partition_name = "_".join([name[:-1], "partition"])
            partition = self.params.get(partition_name, "/run/hosts/*")
            host_list = getattr(self, host_list_name)
            if partition is not None and host_list is None:
                # If a partition is provided instead of a list of hosts use the
                # partition information to populate the list of hosts.
                setattr(self, partition_name, partition)
                setattr(self, host_list_name, get_partition_hosts(partition))
            elif partition is not None and host_list is not None:
                self.fail(
                    "Specifying both a {} partition name and a list of hosts "
                    "is not supported!".format(name))

        # # Find a configuration that meets the test requirements
        # self.config = Configuration(
        #     self.params, self.hostlist_servers, debug=self.debug)
        # if not self.config.set_config(self):
        #     self.cancel("Test requirements not met!")

        # Create host files - In the future this should be the responsibility of
        # tests/classes that need a host file and hostfile_clients should not be
        # a property of this class.
        if self.hostlist_clients:
            self.hostfile_clients = write_host_file(
                self.hostlist_clients, self.workdir,
                self.hostfile_clients_slots)

        # Display host information
        self.log.info("--- HOST INFORMATION ---")
        self.log.info("hostlist_servers:  %s", self.hostlist_servers)
        self.log.info("hostlist_clients:  %s", self.hostlist_clients)
        self.log.info("server_partition:  %s", self.server_partition)
        self.log.info("client_partition:  %s", self.client_partition)

        # Kill commands left running on the hosts (from a previous test) before
        # starting any tests.  Currently only handles 'orterun' processes, but
        # can be expanded.
        hosts = list(self.hostlist_servers)
        if self.hostlist_clients:
            hosts.extend(self.hostlist_clients)
        self.stop_leftover_processes(["orterun"], hosts)

        # Start the clients (agents)
        if self.setup_start_agents:
            self.start_agents()

        # Start the servers
        if self.setup_start_servers:
            self.start_servers()

        # Setup a job manager command for running the test command
        manager_class_name = self.params.get(
            "job_manager_class_name", default=None)
        manager_subprocess = self.params.get(
            "job_manager_subprocess", default=False)
        manager_mpi_type = self.params.get(
            "job_manager_mpi_type", default="mpich")
        if manager_class_name is not None:
            self.job_manager = get_job_manager_class(
                manager_class_name, None, manager_subprocess, manager_mpi_type)

    def stop_leftover_processes(self, processes, hosts):
        """Stop leftover processes on the specified hosts before starting tests.

        Args:
            processes (list): list of process names to stop
            hosts (list): list of hosts on which to stop the leftover processes
        """
        if processes:
            self.log.info(
                "Stopping any of the following commands left running on %s: %s",
                hosts, ",".join(processes))
            stop_processes(hosts, "'({})'".format("|".join(processes)))

    def start_agents(self, agent_groups=None, servers=None):
        """Start the daos_agent processes.

        Args:
            agent_groups (dict, optional): dictionary of lists of hosts on
                which to start the daos agent using a unique server group name
                key. Defaults to None which will use the server group name from
                the test's yaml file to start the daos agents on all client
                hosts specified in the test's yaml file.
            servers (list): list of hosts running the daos servers to be used to
                define the access points in the agent yaml config file

        Raises:
            avocado.core.exceptions.TestFail: if there is an error starting the
                agents

        """
        if agent_groups is None:
            # Include running the daos_agent on the test control host for API
            # calls and calling the daos command from this host.
            agent_groups = {
                self.server_group: include_local_host(self.hostlist_clients)}

        self.log.debug("--- STARTING AGENT GROUPS: %s ---", agent_groups)

        if isinstance(agent_groups, dict):
            for group, hosts in agent_groups.items():
                transport = DaosAgentTransportCredentials(self.workdir)
                # Use the unique agent group name to create a unique yaml file
                config_file = self.get_config_file(group, "agent")
                # Setup the access points with the server hosts
                common_cfg = CommonConfig(group, transport)
                self.add_agent_manager(config_file, common_cfg)
                self.configure_manager(
                    "agent",
                    self.agent_managers[-1],
                    hosts,
                    self.hostfile_clients_slots,
                    servers)
            self.start_agent_managers()

    def start_servers(self, server_groups=None):
        """Start the daos_server processes.

        Args:
            server_groups (dict, optional): dictionary of lists of hosts on
                which to start the daos server using a unique server group name
                key. Defaults to None which will use the server group name from
                the test's yaml file to start the daos server on all server
                hosts specified in the test's yaml file.

        Raises:
            avocado.core.exceptions.TestFail: if there is an error starting the
                servers

        """
        if server_groups is None:
            server_groups = {self.server_group: self.hostlist_servers}

        self.log.debug("--- STARTING SERVER GROUPS: %s ---", server_groups)

        if isinstance(server_groups, dict):
            for group, hosts in server_groups.items():
                transport = DaosServerTransportCredentials(self.workdir)
                # Use the unique agent group name to create a unique yaml file
                config_file = self.get_config_file(group, "server")
                dmg_config_file = self.get_config_file(group, "dmg")
                # Setup the access points with the server hosts
                common_cfg = CommonConfig(group, transport)

                self.add_server_manager(
                    config_file, dmg_config_file, common_cfg)
                self.configure_manager(
                    "server",
                    self.server_managers[-1],
                    hosts,
                    self.hostfile_servers_slots,
                    hosts)
            self.start_server_managers()

    def get_config_file(self, name, command):
        """Get the yaml configuration file.

        Args:
            name (str): unique part of the configuration file name
            command (str): command owning the configuration file

        Returns:
            str: daos_agent yaml configuration file full name

        """
        filename = "{}_{}_{}.yaml".format(self.config_file_base, name, command)
        return os.path.join(self.tmp, filename)

    def add_agent_manager(self, config_file=None, common_cfg=None, timeout=15):
        """Add a new daos agent manager object to the agent manager list.

        Args:
            config_file (str, optional): daos agent config file name. Defaults
                to None, which will use a default file name.
            common_cfg (CommonConfig, optional): daos agent config file
                settings shared between the agent and server. Defaults to None,
                which uses the class CommonConfig.
            timeout (int, optional): number of seconds to wait for the daos
                agent to start before reporting an error. Defaults to 60.
        """
        self.log.info("--- ADDING AGENT MANAGER ---")

        # Setup defaults
        if config_file is None:
            config_file = self.get_config_file("daos", "agent")
        if common_cfg is None:
            agent_transport = DaosAgentTransportCredentials(self.workdir)
            common_cfg = CommonConfig(self.server_group, agent_transport)
        # Create an AgentCommand to manage with a new AgentManager object
        agent_cfg = DaosAgentYamlParameters(config_file, common_cfg)
        agent_cmd = DaosAgentCommand(self.bin, agent_cfg, timeout)
        self.agent_managers.append(
            DaosAgentManager(agent_cmd, self.manager_class))

    def add_server_manager(self, config_file=None, dmg_config_file=None,
                           common_cfg=None, timeout=20):
        """Add a new daos server manager object to the server manager list.

        When adding multiple server managers unique yaml config file names
        and common config setting (due to the server group name) should be used.

        Args:
            config_file (str, optional): daos server config file name. Defaults
                to None, which will use a default file name.
            dmg_config_file (str, optional): dmg config file name. Defaults
                to None, which will use a default file name.
            common_cfg (CommonConfig, optional): daos server config file
                settings shared between the agent and server. Defaults to None,
                which uses the class CommonConfig.
            timeout (int, optional): number of seconds to wait for the daos
                server to start before reporting an error. Defaults to 60.
        """
        self.log.info("--- ADDING SERVER MANAGER ---")

        # Setup defaults
        if config_file is None:
            config_file = self.get_config_file("daos", "server")
        if common_cfg is None:
            common_cfg = CommonConfig(
                self.server_group, DaosServerTransportCredentials(self.workdir))

        if dmg_config_file is None:
            dmg_config_file = self.get_config_file("daos", "dmg")
        transport_dmg = DmgTransportCredentials(self.workdir)
        dmg_cfg = DmgYamlParameters(
            dmg_config_file, self.server_group, transport_dmg)
        # Create a ServerCommand to manage with a new ServerManager object
        server_cfg = DaosServerYamlParameters(config_file, common_cfg)
        server_cmd = DaosServerCommand(self.bin, server_cfg, timeout)
        self.server_managers.append(
            DaosServerManager(server_cmd, self.manager_class, dmg_cfg))

    def configure_manager(self, name, manager, hosts, slots, access_list=None):
        """Configure the agent/server manager object.

        Defines the environment variables, host list, and hostfile settings used
        to start the agent/server manager.

        Args:
            name (str): manager name
            manager (SubprocessManager): the daos agent/server process manager
            hosts (list): list of hosts on which to start the daos agent/server
            slots (int): number of slots per server to define in the hostfile
            access_list (list): list of access point hosts
        """
        self.log.info("--- CONFIGURING %s MANAGER ---", name.upper())
        if access_list is None:
            access_list = self.hostlist_servers
        # Calling get_params() will set the test-specific log names
        manager.get_params(self)
        # Only use the first host in the access list
        manager.set_config_value("access_points", access_list[:1])
        manager.manager.assign_environment(
            EnvironmentVariables({"PATH": None}), True)
        manager.hosts = (hosts, self.workdir, slots)

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
        # We probably want to do this parallel if end up with multiple managers
        for manager in manager_list:
            self.log.info(
                "Starting %s: group=%s, hosts=%s, config=%s",
                name, manager.get_config_value("name"), manager.hosts,
                manager.get_config_value("filename"))
            manager.verify_socket_directory(user)
            manager.start()

    def tearDown(self):
        """Tear down after each test case."""
        # Tear down any test-specific items
        errors = self.pre_tear_down()

        # Stop any test jobs that may still be running
        errors.extend(self.stop_job_managers())

        # Destroy any containers first
        errors.extend(self.destroy_containers(self.container))

        # Destroy any pools next
        errors.extend(self.destroy_pools(self.pool))

        # Stop the agents
        errors.extend(self.stop_agents())

        # Stop the servers
        errors.extend(self.stop_servers())

        # Complete tear down actions from the inherited class
        try:
            super(TestWithServers, self).tearDown()
        except OSError as error:
            errors.append(
                "Error running inherited teardown(): {}".format(error))

        # Fail the test if any errors occurred during tear down
        if errors:
            self.fail(
                "Errors detected during teardown:\n  - {}".format(
                    "\n  - ".join(errors)))

    def pre_tear_down(self):
        """Tear down steps to optionally run before tearDown().

        Returns:
            list: a list of error strings to report at the end of tearDown().

        """
        self.log.info("teardown() started")
        return []

    def stop_job_managers(self):
        """Stop the test job manager.

        Returns:
            list: a list of exceptions raised stopping the agents

        """
        error_list = []
        if self.job_manager:
            self.test_log.info("Stopping test job manager")
            error_list = self._stop_managers(
                [self.job_manager], "test job manager")
        return error_list

    def destroy_containers(self, containers):
        """Close and destroy one or more containers.

        Args:
            containers (object): a list of or single DaosContainer or
                TestContainer object(s) to destroy

        Returns:
            list: a list of exceptions raised destroying the containers

        """
        error_list = []
        if containers:
            if not isinstance(containers, (list, tuple)):
                containers = [containers]
            self.test_log.info("Destroying containers")
            for container in containers:
                # Only close a container that has been opened by the test
                if not hasattr(container, "opened") or container.opened:
                    try:
                        container.close()
                    except (DaosApiError, TestFail) as error:
                        self.test_log.info("  {}".format(error))
                        error_list.append(
                            "Error closing the container: {}".format(error))

                # Only destroy a container that has been created by the test
                if not hasattr(container, "attached") or container.attached:
                    try:
                        container.destroy()
                    except (DaosApiError, TestFail) as error:
                        self.test_log.info("  {}".format(error))
                        error_list.append(
                            "Error destroying container: {}".format(error))
        return error_list

    def destroy_pools(self, pools):
        """Disconnect and destroy one or more pools.

        Args:
            pools (object): a list of or single DaosPool or TestPool object(s)
                to destroy

        Returns:
            list: a list of exceptions raised destroying the pools

        """
        error_list = []
        if pools:
            if not isinstance(pools, (list, tuple)):
                pools = [pools]
            self.test_log.info("Destroying pools")
            for pool in pools:
                # Only disconnect a pool that has been connected by the test
                if not hasattr(pool, "connected") or pool.connected:
                    try:
                        pool.disconnect()
                    except (DaosApiError, TestFail) as error:
                        self.test_log.info("  {}".format(error))
                        error_list.append(
                            "Error disconnecting pool: {}".format(error))

                # Only destroy a pool that has been created by the test
                if not hasattr(pool, "attached") or pool.attached:
                    try:
                        pool.destroy(1)
                    except (DaosApiError, TestFail) as error:
                        self.test_log.info("  {}".format(error))
                        error_list.append(
                            "Error destroying pool: {}".format(error))
        return error_list

    def stop_agents(self):
        """Stop the daos agents.

        Returns:
            list: a list of exceptions raised stopping the agents

        """
        self.test_log.info(
            "Stopping %s group(s) of agents", len(self.agent_managers))
        return self._stop_managers(self.agent_managers, "agents")

    def stop_servers(self):
        """Stop the daos server and I/O servers.

        Returns:
            list: a list of exceptions raised stopping the servers

        """
        self.test_log.info(
            "Stopping %s group(s) of servers", len(self.server_managers))
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

    def update_log_file_names(self, test_name=None):
        """Define agent, server, and client log files that include the test id.

        Args:
            test_name (str, optional): name of test variant
        """
        if test_name:
            # Overwrite the test id with the specified test name
            self.test_id = test_name

        # Update the log file names.  The path is defined through the
        # DAOS_TEST_LOG_DIR environment variable.
        self.agent_log = "{}_daos_agent.log".format(self.test_id)
        self.server_log = "{}_daos_server.log".format(self.test_id)
        self.control_log = "{}_daos_control.log".format(self.test_id)
        self.helper_log = "{}_daos_admin.log".format(self.test_id)
        self.client_log = "{}_daos_client.log".format(self.test_id)
        self.config_file_base = "{}_".format(self.test_id)

    def get_dmg_command(self, index=0):
        """Get a DmgCommand setup to interact with server manager index.

        Return a DmgCommand object configured with:
            - the "-l" parameter assigned to the server's access point list
            - the "-i" parameter assigned to the server's interactive mode

        This method is intended to be used by tests that wants to use dmg to
        create and destroy pool. Pass in the object to TestPool constructor.

        Access point should be passed in to -l regardless of the number of
        servers.

        Args:
            index (int, optional): Server index. Defaults to 0.

        Returns:
            DmgCommand: New DmgCommand object.

        """
        if self.server_managers:
            return self.server_managers[index].dmg

        dmg_config_file = self.get_config_file("daos", "dmg")
        dmg_cfg = DmgYamlParameters(
            dmg_config_file, self.server_group,
            DmgTransportCredentials(self.workdir))
        dmg_cfg.hostlist.update(self.hostlist_servers[:1], "dmg.yaml.hostlist")
        return DmgCommand(self.bin, dmg_cfg)

    def get_daos_command(self):
        """Get a DaosCommand object.

        Returns:
            DaosCommand: New DaosCommand object.

        """
        return DaosCommand(self.bin)

    def prepare_pool(self):
        """Prepare the self.pool TestPool object.

        Create a TestPool object, read the pool parameters from the yaml, create
        the pool, and connect to the pool.

        This sequence is common for a lot of the container tests.
        """
        self.add_pool(None, True, True, 0)

    def get_pool(self, namespace=None, create=True, connect=True, index=0):
        """Get a test pool object.

        This method defines the common test pool creation sequence.

        Args:
            namespace (str, optional): namespace for TestPool parameters in the
                test yaml file. Defaults to None.
            create (bool, optional): should the pool be created. Defaults to
                True.
            connect (bool, optional): should the pool be connected. Defaults to
                True.
            index (int, optional): Server index for dmg command. Defaults to 0.

        Returns:
            TestPool: the created test pool object.

        """
        pool = TestPool(self.context, dmg_command=self.get_dmg_command(index))
        if namespace is not None:
            pool.namespace = namespace
        pool.get_params(self)
        if create:
            pool.create()
        if create and connect:
            pool.connect()
        return pool

    def add_pool(self, namespace=None, create=True, connect=True, index=0):
        """Add a pool to the test case.

        This method defines the common test pool creation sequence.

        Args:
            namespace (str, optional): namespace for TestPool parameters in the
                test yaml file. Defaults to None.
            create (bool, optional): should the pool be created. Defaults to
                True.
            connect (bool, optional): should the pool be connected. Defaults to
                True.
            index (int, optional): Server index for dmg command. Defaults to 0.
        """
        self.pool = self.get_pool(namespace, create, connect, index)

    def get_container(self, pool, namespace=None, create=True):
        """Get a test container object.

        Args:
            pool (TestPool): pool in which to create the container.
            namespace (str, optional): namespace for TestContainer parameters in
                the test yaml file. Defaults to None.
            create (bool, optional): should the container be created. Defaults
                to True.

        Returns:
            TestContainer: the created test container object.

        """
        container = TestContainer(pool, daos_command=self.get_daos_command())
        if namespace is not None:
            container.namespace = namespace
        container.get_params(self)
        if create:
            container.create()
        return container

    def add_container(self, pool, namespace=None, create=True):
        """Add a container to the test case.

        This method defines the common test container creation sequence.

        Args:
            pool (TestPool): pool in which to create the container.
            namespace (str, optional): namespace for TestContainer parameters in
                the test yaml file. Defaults to None.
            create (bool, optional): should the container be created. Defaults
                to True.
        """
        self.container = self.get_container(pool, namespace, create)

    def start_additional_servers(self, additional_servers, index=0):
        """Start additional servers.

        This method can be used to start a new daos_server during a test.

        Args:
            additional_servers (list of str): List of hostnames to start
                daos_server.
            index (int): Determines which server_managers to use when creating
                the new server.
        """
        self.server_managers.append(
            DaosServerManager(
                self.server_managers[index].manager.job,
                self.manager_class,
                self.server_managers[index].dmg.yaml
            )
        )
        self.server_managers[-1].manager.assign_environment(
            EnvironmentVariables({"PATH": None}), True)
        self.server_managers[-1].hosts = (
            additional_servers, self.workdir, self.hostfile_servers_slots)

        self.log.info(
            "Starting %s: group=%s, hosts=%s, config=%s", "server",
            self.server_managers[-1].get_config_value("name"),
            self.server_managers[-1].hosts,
            self.server_managers[-1].get_config_value("filename"))
        self.server_managers[-1].verify_socket_directory(getuser())
        self.server_managers[-1].start()
