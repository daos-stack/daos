#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

# Some useful test classes inherited from avocado.Test
import os
import json
import re

from avocado import Test as avocadoTest
from avocado import skip, TestFail, fail_on
from avocado.utils.distro import detect
from avocado.core import exceptions
from ast import literal_eval

import fault_config_utils
from pydaos.raw import DaosContext, DaosLog, DaosApiError
from command_utils_base import CommandFailure, EnvironmentVariables
from agent_utils import DaosAgentManager, include_local_host
from dmg_utils import get_dmg_command
from daos_utils import DaosCommand
from cart_ctl_utils import CartCtl
from server_utils import DaosServerManager
from general_utils import \
    get_partition_hosts, stop_processes, get_job_manager_class, \
    get_default_config_file, pcmd, get_file_listing, run_command
from logger_utils import TestLogger
from test_utils_pool import TestPool, LabelGenerator
from test_utils_container import TestContainer
from env_modules import load_mpi
from distutils.spawn import find_executable
from write_host_file import write_host_file


def skipForTicket(ticket): # pylint: disable=invalid-name
    """Skip a test with a comment about a ticket."""
    return skip("Skipping until {} is fixed.".format(ticket))


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

    # Skipping Test Variants:
    #   If this list is populated with one or more
    #       [<ticket>, <param_name>, <param_value>]
    #   list items, then setUp() will check each test variant to see if the
    #   <param_name> has been assigned <param_value>.  When this is the case the
    #   test variant will be skipped/cancelled for <ticket> before anything else
    #   in setUp() is executed.  If the <param_name> is "test_method_name" then
    #   <param_value> is compared to the name of the test method.
    CANCEL_FOR_TICKET = []

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)

        # Define a test ID using the test_* method name
        self.test_id = self.get_test_name()

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

        # Support unique test case timeout values.  These test case specific
        # timeouts are read from the test yaml using the test case method name
        # as the key, e.g.:
        #   timeouts:
        #     test_quick: 120
        #     test_long: 1200
        self.timeouts = self.params.get(self.test_id, "/run/timeouts/*")

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
        self._timeout_reported = False
        # When canceling within a test variant,
        # use 'add_cancel_ticket(<ticket>)' to add to this set.
        self._teardown_cancel = set()
        self._teardown_errors = []
        self.basepath = None
        self.prefix = None
        self.ofi_prefix = None
        self.cancel_file = os.path.join(os.sep, "scratch",
                                        "CI-skip-list-master")

    def setUp(self):
        """Set up each test case."""
        # get paths from the build_vars generated by build
        with open('../../.build_vars.json') as build_vars:
            build_paths = json.load(build_vars)
        self.basepath = os.path.normpath(os.path.join(build_paths['PREFIX'],
                                                      '..') + os.path.sep)
        self.prefix = build_paths['PREFIX']
        try:
            self.ofi_prefix = build_paths['OFI_PREFIX']
        except KeyError:
            self.ofi_prefix = os.sep + "usr"
        self.cancel_from_list()
        self.check_variant_skip()
        self.log.info("*** SETUP running on %s ***", str(detect()))
        super().setUp()

    def add_test_data(self, filename, data):
        """Add a file to the test variant specific data directory.

        Args:
            filename (str): name of the file to create
            data (list): data to write to the new file
        """
        try:
            with open(os.path.join(self.outputdir, filename),
                      "w") as file_handle:
                file_handle.writelines(data)
        except IOError as error:
            self.fail("Error writing {}: {}".format(filename, error))

    def cancel_from_list(self):
        """Check if test is in skip list."""
        def skip_process_error(message):
            self.log.info(message)
            self.log.info("Trudging on without skipping known failing tests")

        def cancel_for_ticket(ticket, skip_list):
            # put a copy of the skip-list into the logdir
            self.add_test_data("skip-list", skip_list)
            self.cancelForTicket(ticket)

        try:
            with open(self.cancel_file) as skip_handle:
                skip_list = skip_handle.readlines()
        except Exception as excpt: # pylint: disable=broad-except
            skip_process_error("Unable to read skip list: {}".format(excpt))
            skip_list = []

        for item in skip_list:
            vals = item.split('|')
            skip_it, ticket = self._check_variant_skip(literal_eval(vals[0]))
            if skip_it:
                # test is on the skiplist
                # first see if it's being fixed in this PR
                try:
                    with open(os.path.join(os.sep, 'tmp',
                                           'commit_title')) as commit_handle:
                        if commit_handle.read().strip().startswith(
                                ticket + " "):
                            # fix is in this PR
                            self.log.info("This test variant is included "
                                          "in the skip list for ticket %s, "
                                          "but it is being fixed in this "
                                          "PR.  Test will not be "
                                          "skipped", ticket)
                            return
                except exceptions.TestCancel: # pylint: disable=try-except-raise
                    raise
                except Exception as excpt: # pylint: disable=broad-except
                    skip_process_error("Unable to read commit title: "
                                       "{}".format(excpt))
                # Nope, but there is a commit that fixes it
                # Maybe in this code base, maybe not...
                if len(vals) > 1:
                    try:
                        with open(os.path.join(os.sep, 'tmp',
                                               'commit_list')) as commit_handle:
                            commits = commit_handle.readlines()
                    except Exception as excpt: # pylint: disable=broad-except
                        skip_process_error("Unable to read commit list: "
                                           "{}".format(excpt))
                        commits = None
                    if commits and vals[1] in commits:
                        # fix is in this code base
                        self.log.info("This test variant is included in the "
                                      "skip list for ticket %s, but is fixed "
                                      "in %s.  Test will not be "
                                      "skipped", ticket, vals[1])
                        return
                    # fix is not in this code base
                    self.log.info("Skipping due to being on the "
                                  "skip list for ticket %s, and "
                                  "the fix in %s is not in the "
                                  "current code: %s", ticket, vals[1], commits)
                    cancel_for_ticket(ticket, skip_list)
                else:
                    # there is no commit that fixes it
                    self.log.info("This test variant is included "
                                  "in the skip list for ticket %s "
                                  "with no fix yet "
                                  "available.", ticket)
                    cancel_for_ticket(ticket, skip_list)

    def _check_variant_skip(self, cancel_list):
        """Determine if this test variant should be skipped.

        If cancel_list is populated, check each item in the list to
        determine if this test variant should be skipped (cancelled).  Each item
        should be a tuple whose:
            - first entry is the ticket defining the test variant skip reason
            - next two entries define:
                - the test yaml parameter name to read / test method name
                - the test yaml parameter value used to trigger the skip
        If multiple sets of test yaml names/values are specified they must all
        match in order for the test variant to be skipped.
        """
        for data in (list(item) for item in cancel_list):
            ticket = data.pop(0)
            skip_variant = len(data) > 1
            while data and skip_variant:
                try:
                    name = data.pop(0)
                    value = data.pop(0)
                    if name == "test_method_name":
                        skip_variant &= self.get_test_name() == value
                    else:
                        skip_variant &= self.params.get(name) == value
                except IndexError:
                    self.fail(
                        "Invalid cancel_list format: {}".format(
                            cancel_list))
            return skip_variant, ticket
        return False, ""

    def check_variant_skip(self):
        """Determine if this test variant should be skipped."""
        skip_variant, ticket = self._check_variant_skip(self.CANCEL_FOR_TICKET)
        if skip_variant:
            self.cancelForTicket(ticket)

    # pylint: disable=invalid-name
    def cancelForTicket(self, ticket):
        """Skip a test due to a ticket needing to be completed.

        Args:
            ticket (object): the ticket (str) or group of tickets (set)
                that cause this test case to be cancelled.
        """
        verb = "is"
        if isinstance(ticket, set):
            ticket = sorted(ticket)
            if len(ticket) > 1:
                ticket[-1] = " ".join(["and", ticket[-1]])
                verb = "are"
            ticket = ", ".join(ticket)
        return self.cancel("Skipping until {} {} fixed.".format(ticket, verb))
    # pylint: enable=invalid-name

    def add_cancel_ticket(self, ticket, reason=None):
        """Skip a test due to a ticket needing to be completed.

        Args:
            ticket (object): the ticket (str) used to cancel the test.
            reason (str, option): optional reason to skip. Defaults to None.
        """
        self.log.info(
            "<CANCEL> Skipping %s for %s%s", self.get_test_name(), ticket,
            ": {}".format(reason) if reason else "")
        self._teardown_cancel.add(ticket)

    def get_test_info(self):
        """Get the python file, class, and method from the test name.

        Returns:
            tuple: the test filename, python class, and python method

        """
        keys = ("id", "file", "class", "method", "variant")
        info = [self.name.uid]
        info.extend(self.name.name.split(":"))
        info.extend(info.pop(-1).split("."))
        info.append(self.name.variant)
        return {key: info[index] for index, key in enumerate(keys)}

    def get_test_name(self):
        """Obtain the test method name from the Avocado test name.

        Returns:
            str: name of the test method

        """
        return self.get_test_info()["method"]

    def report_timeout(self):
        """Report whether or not this test case was timed out."""
        if not self._timeout_reported:
            # Mark the beginning of tearDown
            self.log.info("=" * 100)

            # Update the elapsed time
            self.get_state()
            if self.timeout is None:
                # self.timeout is not set - this is a problem
                self.log.error("*** TEARDOWN called with UNKNOWN timeout ***")
                self.log.error("self.timeout undefined - please investigate!")
            elif self.time_elapsed > self.timeout:
                # Timeout has expired
                self.log.info(
                    "*** TEARDOWN called due to TIMEOUT: "
                    "%s second timeout exceeded ***", str(self.timeout))
                self.log.info("test execution has been terminated by avocado")
            else:
                # Normal operation
                remaining = str(self.timeout - self.time_elapsed)
                self.log.info(
                    "*** TEARDOWN called after test completion: elapsed time: "
                    "%s seconds ***", str(self.time_elapsed))
                self.log.info(
                    "Amount of time left in test timeout: %s seconds",
                    remaining)

        # Disable reporting the timeout upon subsequent inherited calls
        self._timeout_reported = True

    def tearDown(self):
        """Tear down after each test case."""
        self.report_timeout()
        super().tearDown()

        # Fail the test if any errors occurred during tear down
        if self._teardown_errors:
            self.fail("Errors detected during teardown:\n - {}".format(
                "\n - ".join(self._teardown_errors)))

        # Cancel the test if any part of the test was skipped due to ticket
        if self._teardown_cancel:
            self.cancelForTicket(self._teardown_cancel)


class TestWithoutServers(Test):
    """Run tests without DAOS servers.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)

        self.client_mca = None
        self.orterun = None
        self.ompi_prefix = None
        self.bin = None
        self.daos_test = None
        self.cart_prefix = None
        self.cart_bin = None
        self.tmp = None
        self.test_dir = os.getenv("DAOS_TEST_LOG_DIR", "/tmp")
        self.fault_file = None
        self.context = None
        self.d_log = None

        # Create a default TestLogger w/o a DaosLog object to prevent errors in
        # tearDown() if setUp() is not completed.  The DaosLog is added upon the
        # completion of setUp().
        self.test_log = TestLogger(self.log, None)

    def setUp(self):
        """Set up run before each test."""
        super().setUp()
        if not load_mpi("openmpi"):
            self.fail("Failed to load openmpi")

        self.orterun = find_executable('orterun')
        if self.orterun is None:
            self.fail("Could not find orterun")

        # hardware tests segfault in MPI_Init without this option
        self.client_mca = "--mca btl_openib_warn_default_gid_prefix 0"
        self.client_mca += " --mca pml ob1"
        self.client_mca += " --mca btl tcp,self"
        self.client_mca += " --mca oob tcp"
        self.ompi_prefix = os.path.dirname(os.path.dirname(self.orterun))
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
        self.log.debug("Shared test directory: %s", self.tmp)
        self.log.debug("Common test directory: %s", self.test_dir)

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
        self.report_timeout()

        if self.fault_file:
            try:
                os.remove(self.fault_file)
            except OSError as error:
                self._teardown_errors.append(
                    "Error running inherited teardown(): {}".format(error))

        super().tearDown()

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


class TestWithServers(TestWithoutServers):
    # pylint: disable=too-many-public-methods,too-many-instance-attributes
    """Run tests with DAOS servers and at least one client.

    Optionally run DAOS clients on specified hosts.  By default run a single
    DAOS client on the host executing the test.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super().__init__(*args, **kwargs)

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
        # Options to control how servers are started for each test variant:
        #   start_servers_once:
        #       True        = start the DAOS servers once per test file allowing
        #                     the same server instances to be used for each test
        #                     variant.
        #       False       = start an new set of DAOS servers for each test
        #                     variant.
        #   server_manager_class / agent_manager_class:
        #       "Orterun"   = use the orterun command to launch DAOS servers /
        #                     agents. Not supported with (start_servers_once set
        #                     to True).
        #       "Systemctl" = use clush and systemctl to launch DAOS servers /
        #                     agents.
        #   setup_start_servers / setup_start_agents:
        #       True        = start the DAOS servers / agents in setUp()
        #       False       = do not start the DAOS servers / agents in setUp()
        #
        # Notes:
        #   - when the setup_start_{servers|agents} is set to False the
        #       start_servers_once attribute will most likely also want to be
        #       set to False to ensure the servers are not running at the start
        #       of each test variant.
        self.start_agents_once = True
        self.start_servers_once = True
        self.server_manager_class = "Systemctl"
        self.agent_manager_class = "Systemctl"
        self.setup_start_servers = True
        self.setup_start_agents = True
        self.hostlist_servers = None
        self.hostlist_clients = None
        self.hostfile_clients = None
        self.server_partition = None
        self.client_partition = None
        self.server_reservation = None
        self.client_reservation = None
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
        # self.debug = False
        # self.config = None
        self.job_manager = None
        self.label_generator = LabelGenerator()

    def setUp(self):
        """Set up each test case."""
        super().setUp()

        # Support starting agents/servers once per test for all test variants
        self.start_agents_once = self.params.get(
            "start_agents_once", "/run/setup/*", self.start_agents_once)
        self.start_servers_once = self.params.get(
            "start_servers_once", "/run/setup/*", self.start_servers_once)

        # Support running servers and agents with different JobManager classes
        self.server_manager_class = self.params.get(
            "server_manager_class", "/run/setup/*", self.server_manager_class)
        self.agent_manager_class = self.params.get(
            "agent_manager_class", "/run/setup/*", self.agent_manager_class)

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
            reservation_name = "_".join([name[:-1], "reservation"])
            reservation_env = "_".join(["DAOS", reservation_name.upper()])
            partition = self.params.get(partition_name, "/run/hosts/*")
            reservation = os.environ.get(reservation_env, None)
            self.log.info("env %s = %s", reservation_env, reservation)
            if reservation is None:
                reservation = self.params.get(reservation_name, "/run/hosts/*")
            host_list = getattr(self, host_list_name)
            if partition is not None and host_list is None:
                # If a partition is provided instead of a list of hosts use the
                # partition information to populate the list of hosts.
                setattr(self, partition_name, partition)
                setattr(self, reservation_name, reservation)
                slurm_nodes = get_partition_hosts(partition, reservation)
                if not slurm_nodes:
                    self.fail(
                        "No valid nodes in {} partition with {} "
                        "reservation".format(partition, reservation))
                setattr(self, host_list_name, slurm_nodes)
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

        # Access points to use by default when starting servers and agents
        self.access_points = self.params.get(
            "access_points", "/run/setup/*", self.hostlist_servers[:1])

        # Display host information
        self.log.info("-" * 100)
        self.log.info("--- HOST INFORMATION ---")
        self.log.info("hostlist_servers:    %s", self.hostlist_servers)
        self.log.info("hostlist_clients:    %s", self.hostlist_clients)
        self.log.info("server_partition:    %s", self.server_partition)
        self.log.info("client_partition:    %s", self.client_partition)
        self.log.info("server_reservation:  %s", self.server_reservation)
        self.log.info("client_reservation:  %s", self.client_reservation)
        self.log.info("access_points:       %s", self.access_points)

        # List common test directory contents before running the test
        self.log.info("-" * 100)
        self.log.debug("Common test directory (%s) contents:", self.test_dir)
        hosts = list(self.hostlist_servers)
        if self.hostlist_clients:
            hosts.extend(self.hostlist_clients)
        lines = get_file_listing(hosts, self.test_dir).stdout_text.splitlines()
        for line in lines:
            self.log.debug("  %s", line)

        if not self.start_servers_once or self.get_test_info()["id"] == 1:
            # Kill commands left running on the hosts (from a previous test)
            # before starting any tests.  Currently only handles 'orterun'
            # processes, but can be expanded.
            hosts = list(self.hostlist_servers)
            if self.hostlist_clients:
                hosts.extend(self.hostlist_clients)
            self.log.info("-" * 100)
            self.stop_leftover_processes(["orterun"], hosts)

            # Ensure write permissions for the daos command log files when
            # using systemctl
            if (self.agent_manager_class == "Systemctl" or
                    self.server_manager_class == "Systemctl"):
                log_dir = os.environ.get("DAOS_TEST_LOG_DIR", "/tmp")
                self.log.info("-" * 100)
                self.log.info(
                    "Updating file permissions for %s for use with systemctl",
                    log_dir)
                pcmd(hosts, "chmod a+rw {}".format(log_dir))

        # Start the servers
        force_agent_start = False
        if self.setup_start_servers:
            force_agent_start = self.start_servers()

        # Start the clients (agents)
        if self.setup_start_agents:
            self.start_agents(force=force_agent_start)

        # If there's no server started, then there's no server log to write to.
        if self.setup_start_servers:

            # Write an ID string to the log file for cross-referencing logs
            # with test ID
            id_str = '"Test.name: ' + str(self) + '"'
            self.write_string_to_logfile(id_str)

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
            self.set_job_manager_timeout()

        # Mark the end of setup
        self.log.info("=" * 100)

    def write_string_to_logfile(self, message):
        """Write a string to the server log.

        The server log message will be appear in the following format:
            <date> <hostname> DAOS[<pid>/0/6] rpc EMIT
                src/cart/crt_rpc.c:53 crt_hdlr_ctl_log_add_msg() <message>

        Args:
            message (str): message to write to log file.
        """
        if self.server_managers and self.agent_managers:
            # Compose and run cart_ctl command
            cart_ctl = CartCtl()
            cart_ctl.add_log_msg.value = "add_log_msg"
            cart_ctl.rank.value = "all"
            cart_ctl.cfg_path.value = "."
            cart_ctl.m.value = message
            cart_ctl.n.value = None

            for manager in self.agent_managers:
                # Fetch attachinfo data from server via the agent
                attachinfo_file = manager.get_attachinfo_file()
                cp_command = "sudo cp {} {}".format(attachinfo_file, ".")
                run_command(cp_command, verbose=True, raise_exception=False)
                cart_ctl.group_name.value = manager.get_config_value("name")
                cart_ctl.run()
        else:
            self.log.info(
                "Unable to write message to the server log: %d servers groups "
                "running / %d agent groups running",
                len(self.server_managers), len(self.agent_managers))

    def set_job_manager_timeout(self):
        """Set the timeout for the job manager.

        Use the following priority when setting the job_manager timeout:
            1) use the test method specific timeout from the test yaml, e.g.
                job_manager_timeout:
                    test_one: 30
                    test_two: 60
            2) use the common job_manager timeout from the test yaml, e.g.
                job_manager_timeout: 45
            3) use the avocado test timeout minus 30 seconds
        """
        if self.job_manager:
            self.job_manager.timeout = self.params.get(
                self.get_test_name(), "/run/job_manager_timeout/*", None)
            if self.job_manager.timeout is None:
                self.job_manager.timeout = self.params.get(
                    "job_manager_timeout", default=None)
                if self.job_manager.timeout is None:
                    self.job_manager.timeout = self.timeout - 30

    def start_agents(self, agent_groups=None, force=False):
        """Start the daos_agent processes.

        Args:
            agent_groups (dict, optional): dictionary of dictionaries,
                containing the list of hosts on which to start the daos agent
                and the list of server access points, using a unique server
                group name key. Defaults to None which will use the server group
                name, all of the client hosts, and the access points from the
                test's yaml file to define a single server group entry.
            force (bool, optional): whether or not to force starting the agents.
                Defaults to False.

        Raises:
            avocado.core.exceptions.TestFail: if there is an error starting the
                agents

        """
        self.setup_agents(agent_groups)
        if self.agent_managers:
            self.start_agent_managers(force)

    def start_servers(self, server_groups=None, force=False):
        """Start the daos_server processes.

        Args:
            server_groups (dict, optional): dictionary of dictionaries,
                containing the list of hosts on which to start the daos server
                and the list of access points, using a unique server group name
                key. Defaults to None which will use the server group name, all
                of the server hosts, and the access points from the test's yaml
                file to define a single server group entry.
            force (bool, optional): whether or not to force starting the
                servers. Defaults to False.

        Raises:
            avocado.core.exceptions.TestFail: if there is an error starting the
                servers

        """
        force_agent_start = False
        self.setup_servers(server_groups)
        if self.server_managers:
            force_agent_start = self.start_server_managers(force)
        return force_agent_start

    def setup_agents(self, agent_groups=None):
        """Start the daos_agent processes.

        Args:
            agent_groups (dict, optional): dictionary of dictionaries,
                containing the list of hosts on which to start the daos agent
                and the list of server access points, using a unique server
                group name key. Defaults to None which will use the server group
                name, all of the client hosts, and the access points from the
                test's yaml file to define a single server group entry.

        Raises:
            avocado.core.exceptions.TestFail: if there is an error starting the
                agents

        """
        if agent_groups is None:
            # Include running the daos_agent on the test control host for API
            # calls and calling the daos command from this host.
            agent_groups = {
                self.server_group: {
                    "hosts": include_local_host(self.hostlist_clients),
                    "access_points": self.access_points
                }
            }

        self.log.info("-" * 100)
        self.log.debug("--- SETTING UP AGENT GROUPS: %s ---", agent_groups)

        if isinstance(agent_groups, dict):
            for group, info in list(agent_groups.items()):
                self.add_agent_manager(group)
                self.configure_manager(
                    "agent",
                    self.agent_managers[-1],
                    info["hosts"],
                    self.hostfile_clients_slots,
                    info["access_points"])

    def setup_servers(self, server_groups=None):
        """Start the daos_server processes.

        Args:
            server_groups (dict, optional): dictionary of dictionaries,
                containing the list of hosts on which to start the daos server
                and the list of access points, using a unique server group name
                key. Defaults to None which will use the server group name, all
                of the server hosts, and the access points from the test's yaml
                file to define a single server group entry.

        Raises:
            avocado.core.exceptions.TestFail: if there is an error starting the
                servers

        """
        if server_groups is None:
            server_groups = {
                self.server_group: {
                    "hosts": self.hostlist_servers,
                    "access_points": self.access_points,
                    "svr_config_file": None,
                    "dmg_config_file": None,
                    "svr_config_temp": None,
                    "dmg_config_temp": None
                }
            }

        self.log.info("-" * 100)
        self.log.debug("--- SETTING UP SERVER GROUPS: %s ---", server_groups)

        if isinstance(server_groups, dict):
            for group, info in list(server_groups.items()):
                self.add_server_manager(
                    group, info["svr_config_file"], info["dmg_config_file"],
                    info["svr_config_temp"], info["dmg_config_temp"])
                self.configure_manager(
                    "server",
                    self.server_managers[-1],
                    info["hosts"],
                    self.hostfile_servers_slots,
                    info["access_points"])

    def get_config_file(self, name, command, path=None):
        """Get the yaml configuration file.

        Args:
            name (str): unique part of the configuration file name
            command (str): command owning the configuration file
            path (str, optional): location for the configuration file. Defaults
                to None which yields the self.tmp shared directory.

        Returns:
            str: daos_agent yaml configuration file full name

        """
        if path is None:
            path = self.tmp
        filename = "{}_{}_{}.yaml".format(self.config_file_base, name, command)
        return os.path.join(path, filename)

    def add_agent_manager(self, group=None, config_file=None, config_temp=None):
        """Add a new daos server manager object to the server manager list.

        Args:
            group (str, optional): server group name. Defaults to None.
            config_file (str, optional): daos_agent configuration file name and
                path. Defaults to None which will use the default filename.
            config_temp (str, optional): file name and path used to generate
                the daos_agent configuration file locally and copy it to all
                the hosts using the config_file specification. Defaults to None.

        Raises:
            avocado.core.exceptions.TestFail: if there is an error specifying
                files to use with the Systemctl job manager class.

        """
        if group is None:
            group = self.server_group
        if config_file is None and self.agent_manager_class == "Systemctl":
            config_file = get_default_config_file("agent")
            config_temp = self.get_config_file(group, "agent", self.test_dir)
        elif config_file is None:
            config_file = self.get_config_file(group, "agent")
            config_temp = None

        # Verify the correct configuration files have been provided
        if self.agent_manager_class == "Systemctl" and config_temp is None:
            self.fail(
                "Error adding a DaosAgentManager: no temporary configuration "
                "file provided for the Systemctl manager class!")

        # Define the location of the certificates
        if self.agent_manager_class == "Systemctl":
            cert_dir = os.path.join(os.sep, "etc", "daos", "certs")
        else:
            cert_dir = self.workdir

        self.agent_managers.append(
            DaosAgentManager(
                group, self.bin, cert_dir, config_file, config_temp,
                self.agent_manager_class, outputdir=self.outputdir)
        )

    def add_server_manager(self, group=None, svr_config_file=None,
                           dmg_config_file=None, svr_config_temp=None,
                           dmg_config_temp=None):
        """Add a new daos server manager object to the server manager list.

        Args:
            group (str, optional): server group name. Defaults to None.
            svr_config_file (str, optional): daos_server configuration file name
                and path. Defaults to None.
            dmg_config_file (str, optional): dmg configuration file name and
                path. Defaults to None.
            svr_config_temp (str, optional): file name and path used to generate
                the daos_server configuration file locally and copy it to all
                the hosts using the config_file specification. Defaults to None.
            dmg_config_temp (str, optional): file name and path used to generate
                the dmg configuration file locally and copy it to all the hosts
                using the config_file specification. Defaults to None.

        Raises:
            avocado.core.exceptions.TestFail: if there is an error specifying
                files to use with the Systemctl job manager class.

        """
        if group is None:
            group = self.server_group
        if svr_config_file is None and self.server_manager_class == "Systemctl":
            svr_config_file = get_default_config_file("server")
            svr_config_temp = self.get_config_file(
                group, "server", self.test_dir)
        elif svr_config_file is None:
            svr_config_file = self.get_config_file(group, "server")
            svr_config_temp = None
        if dmg_config_file is None and self.server_manager_class == "Systemctl":
            dmg_config_file = get_default_config_file("control")
            dmg_config_temp = self.get_config_file(group, "dmg", self.test_dir)
        elif dmg_config_file is None:
            dmg_config_file = self.get_config_file(group, "dmg")
            dmg_config_temp = None

        # Verify the correct configuration files have been provided
        if self.server_manager_class == "Systemctl" and svr_config_temp is None:
            self.fail(
                "Error adding a DaosServerManager: no temporary configuration "
                "file provided for the Systemctl manager class!")

        # Define the location of the certificates
        if self.server_manager_class == "Systemctl":
            svr_cert_dir = os.path.join(os.sep, "etc", "daos", "certs")
            dmg_cert_dir = os.path.join(os.sep, "etc", "daos", "certs")
        else:
            svr_cert_dir = self.workdir
            dmg_cert_dir = self.workdir

        self.server_managers.append(
            DaosServerManager(
                group, self.bin, svr_cert_dir, svr_config_file, dmg_cert_dir,
                dmg_config_file, svr_config_temp, dmg_config_temp,
                self.server_manager_class)
        )

    def configure_manager(self, name, manager, hosts, slots,
                          access_points=None):
        """Configure the agent/server manager object.

        Defines the environment variables, host list, and hostfile settings used
        to start the agent/server manager.

        Args:
            name (str): manager name
            manager (SubprocessManager): the daos agent/server process manager
            hosts (list): list of hosts on which to start the daos agent/server
            slots (int): number of slots per engine to define in the hostfile
            access_points (list, optional): list of access point hosts. Defaults
                to None which uses self.access_points.
        """
        self.log.info("-" * 100)
        self.log.info("--- CONFIGURING %s MANAGER ---", name.upper())
        if access_points is None:
            access_points = self.access_points
        # Calling get_params() will set the test-specific log names
        manager.get_params(self)
        manager.set_config_value("access_points", access_points)
        manager.manager.assign_environment(
            EnvironmentVariables({"PATH": None}), True)
        manager.hosts = (hosts, self.workdir, slots)

    @fail_on(CommandFailure)
    def start_agent_managers(self, force=False):
        """Start the daos_agent processes on each specified list of hosts.

        Args:
            force (bool, optional): whether or not to force starting the agents.
                Defaults to False.
        """
        # Determine if all the expected agents are currently running
        status = self.check_running("agents", self.agent_managers, False, True)

        # Start/restart the agents
        if force or status["restart"] or not self.start_agents_once:
            # Stop any running agents
            self.log.info("-" * 100)
            self.log.info("--- STOPPING AGENTS ---")
            self.test_log.info(
                "Stopping %s group(s) of agents", len(self.agent_managers))
            self._stop_managers(self.agent_managers, "agents")

            # Start the agents
            self.log.info("-" * 100)
            self.log.info("--- STARTING AGENTS ---")
            self._start_manager_list("agent", self.agent_managers)

        elif self.start_agents_once:
            self.log.info(
                "All %s groups(s) of agents currently running",
                len(self.agent_managers))

    @fail_on(CommandFailure)
    def start_server_managers(self, force=False):
        """Start the daos_server processes on each specified list of hosts.

        Args:
            force (bool, optional): whether or not to force starting the
                servers. Defaults to False.

        Returns:
            bool: whether or not to force the starting of the agents

        """
        force_agent_start = False

        # Determine if all the expected servers are currently running
        status = self.check_running(
            "servers", self.server_managers, True, True)

        # Start/restart the severs
        if force or status["restart"] or not self.start_servers_once:
            # Stop any running servers
            self.log.info("-" * 100)
            self.log.info("--- STOPPING SERVERS ---")
            self.test_log.info(
                "Stopping %s group(s) of servers", len(self.server_managers))
            self._stop_managers(self.server_managers, "servers")

            # Start the servers
            self.log.info("-" * 100)
            self.log.info("--- STARTING SERVERS ---")
            self._start_manager_list("server", self.server_managers)

            # Force agent restart whenever servers are restarted
            force_agent_start = True
            self.log.info(
                "-- Forcing the start/restart of agents due to the server "
                "start/restart --")

        elif self.start_servers_once:
            self.log.info(
                "All %s groups(s) of servers currently running",
                len(self.server_managers))

        return force_agent_start

    def check_running(self, name, manager_list, prepare_dmg=False,
                      set_expected=False):
        """Verify that agents/servers are running on all the expected hosts.

        Args:
            name (str): manager name
            manager_list (list): list of SubprocessManager objects to start
            prepare_dmg (bool, optional): option to prepare the dmg command for
                each server manager prior to querying the server states. This
                should be set to True when verifying server states for servers
                started by other test variants. Defaults to False.
            set_expected (bool, optional): option to update the expected rank
                states to the current states prior to check. Defaults to False.

        Returns:
            dict: a dictionary of whether or not any of the states were not
                'expected' (which should warrant an error) and whether or the
                agents/servers require a 'restart' (either due to any unexpected
                states or because at least one agent/server was found not to be
                running)

        """
        status = {"expected": True, "restart": False}
        self.log.info("-" * 100)
        self.log.info(
            "--- VERIFYING STATES OF %s %s GROUP%s ---",
            len(manager_list), name.upper(),
            "S" if len(manager_list) > 1 else "")
        for manager in manager_list:
            # Setup the dmg command
            if prepare_dmg and hasattr(manager, "prepare_dmg"):
                manager.prepare_dmg()

            # Verify the current states match the expected states
            manager_status = manager.verify_expected_states(set_expected)
            status["expected"] &= manager_status["expected"]
            if manager_status["restart"]:
                status["restart"] = True

        return status

    def _start_manager_list(self, name, manager_list):
        """Start each manager in the specified list.

        Args:
            name (str): manager name
            manager_list (list): list of SubprocessManager objects to start
        """
        # We probably want to do this parallel if end up with multiple managers
        for manager in manager_list:
            self.log.info(
                "Starting %s: group=%s, hosts=%s, config=%s",
                name, manager.get_config_value("name"), manager.hosts,
                manager.get_config_value("filename"))
            manager.start()

    def tearDown(self):
        """Tear down after each test case."""
        # Report whether or not the timeout has expired
        self.report_timeout()

        # Tear down any test-specific items
        self._teardown_errors = self.pre_tear_down()

        # Stop any test jobs that may still be running
        self._teardown_errors.extend(self.stop_job_managers())

        # Destroy any containers first
        self._teardown_errors.extend(self.destroy_containers(self.container))

        # Destroy any pools next
        self._teardown_errors.extend(self.destroy_pools(self.pool))

        # Stop the agents
        self._teardown_errors.extend(self.stop_agents())

        # Stop the servers
        self._teardown_errors.extend(self.stop_servers())

        super().tearDown()

    def pre_tear_down(self):
        """Tear down steps to optionally run before tearDown().

        Returns:
            list: a list of error strings to report at the end of tearDown().

        """
        self.log.debug("no pre-teardown steps defined")
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
        self.log.info("-" * 100)
        self.log.info("--- STOPPING AGENTS ---")
        errors = []
        status = self.check_running("agents", self.agent_managers)
        if self.start_agents_once and not status["restart"]:
            self.log.info(
                "Agents are configured to run across multiple test variants, "
                "not stopping")
        else:
            if not status["expected"]:
                errors.append(
                    "ERROR: At least one multi-variant agent was not found in "
                    "its expected state; stopping all agents")
            self.test_log.info(
                "Stopping %s group(s) of agents", len(self.agent_managers))
            errors.extend(self._stop_managers(self.agent_managers, "agents"))
        return errors

    def stop_servers(self):
        """Stop the daos server and I/O Engines.

        Returns:
            list: a list of exceptions raised stopping the servers

        """
        self.log.info("-" * 100)
        self.log.info("--- STOPPING SERVERS ---")
        errors = []
        status = self.check_running("servers", self.server_managers)
        if self.start_servers_once and not status["restart"]:
            self.log.info(
                "Servers are configured to run across multiple test variants, "
                "not stopping")
        else:
            if not status["expected"]:
                errors.append(
                    "ERROR: At least one multi-variant server was not found in "
                    "its expected state; stopping all servers")
            self.test_log.info(
                "Stopping %s group(s) of servers", len(self.server_managers))
            errors.extend(self._stop_managers(self.server_managers, "servers"))

            # Stopping agents whenever servers are stopped for DAOS-6873
            self.log.info(
                "Workaround for DAOS-6873: Stopping %s group(s) of agents",
                len(self.agent_managers))
            errors.extend(self._stop_managers(self.agent_managers, "agents"))
        return errors

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

        if self.server_manager_class == "Systemctl":
            dmg_config_file = get_default_config_file("control")
            dmg_config_temp = self.get_config_file("daos", "dmg", self.test_dir)
            dmg_cert_dir = os.path.join(os.sep, "etc", "daos", "certs")
        else:
            dmg_config_file = self.get_config_file("daos", "dmg")
            dmg_config_temp = None
            dmg_cert_dir = self.workdir

        dmg_cmd = get_dmg_command(
            self.server_group, dmg_cert_dir, self.bin, dmg_config_file,
            dmg_config_temp)
        dmg_cmd.hostlist = self.access_points
        return dmg_cmd

    def get_daos_command(self):
        """Get a DaosCommand object.

        Returns:
            DaosCommand: a new DaosCommand object

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
        pool = TestPool(
            context=self.context, dmg_command=self.get_dmg_command(index),
            label_generator=self.label_generator)
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

    def add_pool_qty(self, quantity, namespace=None, create=True, connect=True,
                     index=0):
        """Add multiple pools to the test case.

        This method requires self.pool to be defined as a list.  If self.pool is
        undefined it will define it as a list.

        Args:
            quantity (int): number of pools to create
            namespace (str, optional): namespace for TestPool parameters in the
                test yaml file. Defaults to None.
            create (bool, optional): should the pool be created. Defaults to
                True.
            connect (bool, optional): should the pool be connected. Defaults to
                True.
            index (int, optional): Server index for dmg command. Defaults to 0.

        Raises:
            TestFail: if self.pool is defined, but not as a list object.

        """
        if self.pool is None:
            self.pool = []
        if not isinstance(self.pool, list):
            self.fail(
                "add_pool_qty(): self.pool must be a list: {}".format(
                    type(self.pool)))
        for _ in range(quantity):
            self.pool.append(self.get_pool(namespace, create, connect, index))

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

    def start_additional_servers(self, additional_servers, index=0,
                                 access_points=None):
        """Start additional servers.

        This method can be used to start a new daos_server during a test.

        Args:
            additional_servers (list of str): List of hostnames to start
                daos_server.
            index (int): Determines which server_managers to use when creating
                the new server.
            access_points (list, optional): list of access point hosts. Defaults
                to None which uses self.access_points.
        """
        self.add_server_manager(
            self.server_managers[index].manager.job.get_config_value("name"),
            self.server_managers[index].manager.job.yaml.filename,
            self.server_managers[index].dmg.yaml.filename,
            self.server_managers[index].manager.job.temporary_file,
            self.server_managers[index].dmg.temporary_file
        )
        self.configure_manager(
            "server",
            self.server_managers[-1],
            additional_servers,
            self.hostfile_servers_slots,
            access_points
        )
        self._start_manager_list("server", [self.server_managers[-1]])
