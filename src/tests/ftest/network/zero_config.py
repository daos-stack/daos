#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
from avocado import fail_on
from apricot import TestWithServers
from daos_racer_utils import DaosRacerCommand
from command_utils import CommandFailure


class ZeroConfigTest(TestWithServers):
    """Test class for zero-config tests.

    Test Class Description:
        Test to verify that client application to libdaos can access a running
        DAOS system with & without any special environment variable definitions.

    :avocado: recursive
    """

    def get_port_cnt(self, dev_names, port_counter):
        """Get the port count info for device names specified.

        Args:
            dev_names (list): list of devices to get counter information for.
            port_counter (str): port counter to get information from

        Returns:
            list: count values specifying the port_counter value respective to
                dev_names passed.

        """
        for dev in dev_names:
            if listdir("/sys/class/infiniband/"):

"cat /sys/class/infiniband/hfi1_0/ports/*/counters/port_rcv_data"
        return []

    @fail_on(CommandFailure)
    def verify_client_run(self, server_idx, exp_iface, env=False):
        """Verify the interface assigned by runnning a libdaos client.

        Args:
            server_idx (int): server to get config from.
            env (bool): add OFI_INTERFACE variable to exported variables of
                client command. Defaults to False.
        """
        # Get counter values for hfi devices before and after
        hfi_map = {"hfi1_0": "ib0", "hfi1_1": "ib1"}
        cnt_before = self.get_port_cnt(hfi_map.keys(), "port_rcv_data")

        # Let's run daos_racer as a client
        daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0])
        daos_racer.get_params(self)

        # Update env_name list to add OFI_INTERFACE if needed.
        if env:
            self.update_env_names(["OFI_INTERFACE"])
        daos_racer.set_environment(
            daos_racer.get_environment(self.server_managers[server_idx]))
        daos_racer.run()

        # Verify output and port count to check what iface CaRT init with.
        cnt_after = self.get_port_cnt(hfi_map.keys(), "port_rcv_data")

        for i, dev in enumerate(hfi_map):
            diff = cnt_after[i] - cnt_before[i]
            self.log.info("%s port count difference: %s", dev, diff)


    def test_env_set_unset(self):
        """JIRA ID: DAOS-4880.

        Test Description:
            Test starting a single daos_server process on 2 different numa
            nodes and verify that client can start when OFI_INTERFACE is set.

        :avocado: tags=all,pr,hw,small,zero_config,env_set
        """
        env_state = self.params.get("env_state", '/run/zero_config/*')
        for idx, exp_iface in enumerate(["ib0", "ib1"]):
            # Configure the daos server
            config_file = self.get_config_file(self.server_group, "server")
            self.add_server_manager(config_file)
            self.configure_manager(
                "server",
                self.server_managers[idx],
                self.hostlist_servers,
                self.hostfile_servers_slots,
                self.hostlist_servers)
            self.assertTrue(
                self.server_managers[idx].set_config_value(
                    "pinned_numa_node", idx),
                "Error updating daos_server 'pinned_numa_node' config opt")

            # Start the daos server
            self.start_server_managers()

            # Verify
            err = []
            if not self.verify_client_run(idx, exp_iface, env_state):
                err.append("Failed to run with expected: {}".format(exp_iface))

        self.assertEqual(len(err), 0, "{}".format("\n".join(err)))
