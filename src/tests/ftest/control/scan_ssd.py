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
from getpass import getuser

from apricot import TestWithServers
from server_utils import DaosServerCommand
from server_utils_params import DaosServerTransportCredentials
from command_utils_base import CommonConfig


class ScanSSDTest(TestWithServers):
    """Test Class Description: Verify that storage scan outputs the same NVMe
    info before starting daos_server, after starting, but before format, and
    after format.

    :avocado: recursive
    """

    def test_scan_ssd(self):
        """
        JIRA ID: DAOS-3584

        Test Description: Test dmg storage scan with and without --verbose.

        :avocado: tags=all,small,full_regression,hw,control,scan_ssd
        """
        # Call daos_server storage scan and get the expected NVMe data
        # Prepare to start server. Most of these are from test.py
        transport = DaosServerTransportCredentials(self.workdir)
        # Use the unique agent group name to create a unique yaml file
        config_file = self.get_config_file(self.server_group, "server")
        dmg_config_file = self.get_config_file(self.server_group, "dmg")
        # Setup the access points with the server hosts
        common_cfg = CommonConfig(self.server_group, transport)
        self.add_server_manager(config_file, dmg_config_file, common_cfg)
        self.configure_manager(
            "server", self.server_managers[-1], self.hostlist_servers,
            self.hostfile_servers_slots, self.hostlist_servers)

        # Call daos_server storage scan
        server_cmd = DaosServerCommand(path=self.bin)
        ds_scan = server_cmd.storage_scan()
        pci_addrs = ds_scan["nvme"].keys()

        # Start daos_server, but don't format storage
        self.log.info(
            "Starting server: group=%s, hosts=%s, config=%s",
            self.server_managers[0].get_config_value("name"),
            self.server_managers[0].hosts,
            self.server_managers[0].get_config_value("filename"))
        self.server_managers[0].verify_socket_directory(getuser())
        self.server_managers[0].prepare()
        self.server_managers[0].detect_format_ready()
        self.log.info("daos_server started and waiting for format")

        # Call dmg storage scan --verbose before format. The table in the output
        # should be the same as the output from daos_server storage scan. The
        # only difference is that dmg has hostname at the top
        dmg_scan_before = self.server_managers[0].dmg.storage_scan(verbose=True)

        # Format storage and wait for server to change ownership
        self.log.info(
            "<SERVER> Formatting hosts: <%s>",
            self.server_managers[0].dmg.hostlist)
        self.server_managers[0].dmg.storage_format()
        # Wait for all the doas_io_servers to start
        self.server_managers[0].detect_io_server_start()
        self.log.info("Storage format complete")

        # Call dmg storage scan --verbose after format
        dmg_scan_after = self.server_managers[0].dmg.storage_scan(verbose=True)

        # Verify the 3 dicts are the same. Verify all the columns of each PCI
        # address row
        for pci_addr in pci_addrs:
            ds_data = ds_scan["nvme"][pci_addr]
            dmg_before_data = dmg_scan_before[self.hostlist_servers[0]]["nvme"]\
                [pci_addr]
            dmg_after_data = dmg_scan_after[self.hostlist_servers[0]]["nvme"]\
                [pci_addr]
            self.assertEqual(
                ds_data, dmg_before_data,
                "dmg before is different from daos_server! " +
                "dmg = {}; ds = {}".format(dmg_before_data, ds_data))
            self.assertEqual(
                ds_data, dmg_after_data,
                "dmg after is different from daos_server! " +
                "dmg = {}; ds = {}".format(dmg_after_data, ds_data))
