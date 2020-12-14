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
from __future__ import print_function

import os
import re
import random
from avocado import fail_on
from apricot import TestWithServers
from daos_racer_utils import DaosRacerCommand
from agent_utils import include_local_host
from command_utils import CommandFailure
from general_utils import check_file_exists, get_host_data, get_log_file


class ZeroConfigTest(TestWithServers):
    """Test class for zero-config tests.

    Test Class Description:
        Test to verify that client application to libdaos can access a running
        DAOS system with & without any special environment variable definitions.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for zero-config test."""
        self.setup_start_servers = False
        super(ZeroConfigTest, self).setUp()

    def get_port_cnt(self, hosts, dev, port_counter):
        """Get the port count info for device names specified.

        Args:
            hosts (list): list of hosts
            dev (str): device to get counter information for
            port_counter (str): port counter to get information from

        Returns:
            dict: a dictionary of data values for each NodeSet key

        """
        b_path = "/sys/class/infiniband/{}".format(dev)
        file = os.path.join(b_path, "ports/1/counters", port_counter)

        # Check if if exists for the host
        check_result = check_file_exists(hosts, file)
        if not check_result[0]:
            self.fail("{}: {} not found".format(check_result[1], file))

        cmd = "cat {}".format(file)
        text = "port_counter"
        error = "Error obtaining {} info".format(port_counter)
        return get_host_data(hosts, cmd, text, error, 20)

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
        for output in get_host_data(hosts, cmd, log_file, err).values():
            detected = len(re.findall(pattern, output))
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
        hfi_map = {"ib0": "hfi1_0", "ib1": "hfi1_1"}

        # Get counter values for hfi devices before and after
        cnt_before = self.get_port_cnt(
            self.hostlist_clients, hfi_map[exp_iface], "port_rcv_data")

        # get the dmg config file for daos_racer
        dmg = self.get_dmg_command()

        # Let's run daos_racer as a client
        daos_racer = DaosRacerCommand(self.bin,
                                      self.hostlist_clients[0], dmg)
        daos_racer.get_params(self)

        # Update env_name list to add OFI_INTERFACE if needed.
        if env:
            daos_racer.update_env_names(["OFI_INTERFACE"])

        # Setup the environment and logfile
        logf = "daos_racer_{}_{}.log".format(exp_iface, env)

        # Add FI_LOG_LEVEL to get more info on device issues
        racer_env = daos_racer.get_environment(self.server_managers[0], logf)
        racer_env["FI_LOG_LEVEL"] = "info"
        racer_env["D_LOG_MASK"] = "INFO,object=ERR,placement=ERR"
        daos_racer.set_environment(racer_env)

        # Run client
        daos_racer.run()

        # Verify output and port count to check what iface CaRT init with.
        cnt_after = self.get_port_cnt(
            self.hostlist_clients, hfi_map[exp_iface], "port_rcv_data")

        diff = 0
        for cnt_b, cnt_a in zip(cnt_before.values(), cnt_after.values()):
            diff = int(cnt_a) - int(cnt_b)
            self.log.info("Port [%s] count difference: %s", exp_iface, diff)

        # Read daos.log to verify device used and prevent false positives
        self.assertTrue(
            self.get_log_info(
                self.hostlist_clients, exp_iface, env, get_log_file(logf)))

        # If we don't see data going through the device, fail
        status = True
        if diff <= 0:
            self.log.info("No traffic seen through device: %s", exp_iface)
            status = False
        else:
            status = True
        return status

    def test_env_set_unset(self):
        """JIRA ID: DAOS-4880.

        Test Description:
            Test starting a daos_server process on 2 different numa
            nodes and verify that client can start when OFI_INTERFACE is set
            or unset. The test expects that the server will have two interfaces
            available: hfi_0 and hfi_1.

        :avocado: tags=all,pr,hw,small,zero_config,env_set
        """
        env_state = self.params.get("env_state", '/run/zero_config/*')
        dev_info = {"ib0": 0, "ib1": 1}
        exp_iface = random.choice(dev_info.keys())

        # Configure the daos server
        config_file = self.get_config_file(self.server_group, "server")
        self.add_server_manager(config_file)
        self.configure_manager(
            "server",
            self.server_managers[0],
            self.hostlist_servers,
            self.hostfile_servers_slots,
            self.hostlist_servers)
        self.assertTrue(
            self.server_managers[0].set_config_value(
                "fabric_iface", exp_iface),
            "Error updating daos_server 'fabric_iface' config opt")
        self.assertTrue(
            self.server_managers[0].set_config_value(
                "pinned_numa_node", dev_info[exp_iface]),
            "Error updating daos_server 'pinned_numa_node' config opt")

        # Start the daos server
        self.start_server_managers()

        # Verify
        err = []
        if not self.verify_client_run(exp_iface, env_state):
            err.append("Failed run with expected dev: {}".format(exp_iface))

        self.assertEqual(len(err), 0, "{}".format("\n".join(err)))
