#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import os
import re
import random
from avocado import fail_on
from apricot import TestWithServers
from daos_racer_utils import DaosRacerCommand
from exception_utils import CommandFailure
from general_utils import check_file_exists, get_host_data, get_log_file, run_pcmd


class ZeroConfigTest(TestWithServers):
    """Test class for zero-config tests.

    Test Class Description:
        Test to verify that client application to libdaos can access a running
        DAOS system with & without any special environment variable definitions.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize the ZeroConfigTest class."""
        super().__init__(*args, **kwargs)
        self.interfaces = {}
        self.start_agents_once = False
        self.start_servers_once = False
        self.setup_start_servers = False
        self.setup_start_agents = False

    def get_device_info(self):
        """Get the available device names, their numa nodes, and their domains."""
        self.interfaces = {}
        command = "ls -1 {}".format(os.path.join(os.path.sep, "sys", "class", "net"))
        results = run_pcmd(self.hostlist_servers, command)
        if len(results) != 1:
            self.fail("Error obtaining interfaces - non-homogeneous config")
        try:
            # Find any ib* device in the listing and initially use default numa and domain values
            for index, interface in enumerate(re.findall(r"ib\d", "\n".join(results[0]["stdout"]))):
                self.interfaces[interface] = {
                    "numa": index, "domain": "hfi1_{}".format(index), "port": "1"}
        except (IndexError, KeyError) as error:
            self.log.error("Error obtaining interfaces: %s", str(error))
            self.fail("Error obtaining interfaces - unexpected error")

        # Update interface domain and NUMA node settings for Mellanox devices through mst output:
        #   DEVICE_TYPE        MST   PCI       RDMA     NET       NUMA
        #   ConnectX6(rev:0)   NA    86:00.0   mlx5_1   net-ib1   1
        #   ConnectX6(rev:0)   NA    37:00.0   mlx5_0   net-ib0   0
        command = "sudo mst status -v"
        results = run_pcmd(self.hostlist_servers, command)
        try:
            if results[0]["exit_status"] == 0:
                regex = r"(mlx\d_\d)\s+net-(ib\d)\s+(\d)"
                for match in re.findall(regex, "\n".join(results[0]["stdout"])):
                    self.interfaces[match[1]]["numa"] = int(match[2])
                    self.interfaces[match[1]]["domain"] = match[0]
                    self.interfaces[match[1]]["port"] = "1"
        except (IndexError, KeyError, ValueError) as error:
            self.log.error("Error obtaining interfaces: %s", str(error))
            self.fail("Error obtaining interfaces - unexpected error")

        if not self.interfaces:
            self.fail("No ib* interfaces found!")

    def get_port_cnt(self, hosts, port_counter):
        """Get the port count info for device names specified.

        Args:
            hosts (list): list of hosts
            port_counter (str): port counter information to collect

        Returns:
            dict: a dictionary of the requested port data for each interface on each host

        """
        port_info = {}
        for interface in self.interfaces:
            # Check the port counter for each interface on all of the hosts
            counter_file = os.path.join(
                os.sep, "sys", "class", "infiniband", self.interfaces[interface]["domain"], "ports",
                self.interfaces[interface]["port"], "counters", port_counter)
            check_result = check_file_exists(hosts, counter_file)
            if not check_result[0]:
                self.fail("{}: {} not found".format(check_result[1], counter_file))
            all_host_data = get_host_data(
                hosts, "cat {}".format(counter_file), "{} port_counter".format(interface),
                "Error obtaining {} info".format(port_counter), 20)
            port_info[interface] = {}
            for host_data in all_host_data:
                for host in list(host_data["hosts"]):
                    port_info[interface][host] = {1: {port_counter: host_data["data"]}}
        return port_info

    def get_log_info(self, hosts, dev, env_state, log_file):
        """Get information from daos.log file to verify device used.

        Args:
            hosts (list): list of hosts
            dev (str): device to get counter information for
            env_state (bool): set state for OFI_INTERFACE env variable
            log_file (str): log file to verify

        Returns:
            bool: status of whether correct device was used.

        """
        # anticipate log switch
        cmd = "if [ -f {0}.old ]; then head -50 {0}.old; else head -50 {0};" \
              "fi".format(log_file)
        err = "Error getting log data."
        pattern = r"Using\s+client\s+provided\s+OFI_INTERFACE:\s+{}".format(dev)

        detected = 0
        for host_data in get_host_data(hosts, cmd, log_file, err):
            detected = len(re.findall(pattern, host_data["data"]))
        self.log.info(
            "Found %s instances of client setting up OFI_INTERFACE=%s",
            detected, dev)

        # Verify
        status = True
        if env_state and detected != 1:
            status = False
        elif not env_state and detected == 1:
            status = False
        return status

    @fail_on(CommandFailure)
    def verify_client_run(self, exp_iface, env):
        """Verify the interface assigned by running a libdaos client.

        Args:
            exp_iface (str): expected interface to check.
            env (bool): add OFI_INTERFACE variable to exported variables of
                client command.

        Returns:
            bool: returns status

        """
        clients = self.agent_managers[0].hosts

        # Get counter values for hfi devices before and after
        port_info_before = self.get_port_cnt(clients, "port_rcv_data")

        # get the dmg config file for daos_racer
        dmg = self.get_dmg_command()

        # Let's run daos_racer as a client
        daos_racer = DaosRacerCommand(self.bin, clients[0], dmg)
        daos_racer.get_params(self)

        # Update env_name list to add OFI_INTERFACE if needed.
        if env:
            daos_racer.update_env_names(["OFI_INTERFACE"])

        # Setup the environment and logfile
        log_file = "daos_racer_{}_{}.log".format(exp_iface, env)

        # Add FI_LOG_LEVEL to get more info on device issues
        exp_iface_port = []
        if "ucx" in self.server_managers[0].get_config_value("provider"):
            exp_iface_port = [self.interfaces[exp_iface]["port"]]
        racer_env = daos_racer.get_environment(self.server_managers[0], log_file)
        racer_env["FI_LOG_LEVEL"] = "info"
        racer_env["D_LOG_MASK"] = "INFO,object=ERR,placement=ERR"
        racer_env["OFI_DOMAIN"] = ":".join([self.interfaces[exp_iface]["domain"]] + exp_iface_port)
        daos_racer.set_environment(racer_env)

        # Run client
        daos_racer.run()

        # Verify output and port count to check what iface CaRT init with.
        port_info_after = self.get_port_cnt(clients, "port_rcv_data")

        self.log.info("Client interface port_rcv_data counters")
        msg_format = "%16s  %9s  %9s  %9s  %s"
        self.log.info(msg_format, "Host(s)", "Interface", "Before", "After", "Difference")
        self.log.info(msg_format, "-" * 16, "-" * 9, "-" * 9, "-" * 9, "-" * 9)
        no_traffic = set()
        for interface in sorted(port_info_before):
            for host in sorted(port_info_before[interface]):
                before = port_info_before[interface][host][1]["port_rcv_data"]
                try:
                    after = port_info_after[interface][host][1]["port_rcv_data"]
                    diff = int(after) - int(before)
                    if diff <= 0:
                        no_traffic.add(interface)
                except (KeyError, ValueError) as error:
                    after = "Error"
                    diff = "Unknown - {}".format(error)
                    no_traffic.add(interface)
                self.log.info(msg_format, host, interface, before, after, diff)

        # Read daos.log to verify device used and prevent false positives
        self.assertTrue(self.get_log_info(clients, exp_iface, env, get_log_file(log_file)))

        # If we don't see data going through the device, fail
        for interface in no_traffic:
            self.log.info("No client traffic seen through device: %s", interface)
        return len(no_traffic) != len(self.interfaces)

    def test_env_set_unset(self):
        """JIRA ID: DAOS-4880.

        Test Description:
            Test starting a daos_server process on 2 different numa
            nodes and verify that client can start when OFI_INTERFACE is set
            or unset. The test expects that the server will have two interfaces
            available: hfi_0 and hfi_1.

        :avocado: tags=all,daily_regression,hw,small,zero_config,env_set
        """
        env_state = self.params.get("env_state", '/run/zero_config/*')

        # Get the available interfaces and their domains
        self.get_device_info()
        exp_iface = random.choice(list(self.interfaces.keys())) #nosec

        # Configure the daos server
        self.setup_servers()
        self.assertTrue(
            self.server_managers[0].set_config_value(
                "fabric_iface", exp_iface),
            "Error updating daos_server 'fabric_iface' config opt")
        self.assertTrue(
            self.server_managers[0].set_config_value(
                "pinned_numa_node", self.interfaces[exp_iface]["numa"]),
            "Error updating daos_server 'pinned_numa_node' config opt")

        # Start the daos server
        self.start_server_managers()

        # Start the daos agents - do not start an agent on the local host
        agent_groups = {
            self.server_group: {
                "hosts": self.hostlist_clients,
                "access_points": self.access_points}
        }
        self.start_agents(agent_groups)
        self.log.info("-" * 100)

        # Verify
        if not self.verify_client_run(exp_iface, env_state):
            self.fail("Failed run with expected dev: {}".format(exp_iface))
        self.log.info("Test passed!")
