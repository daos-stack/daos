#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from general_utils import run_command
from ior_test_base import IorTestBase
from apricot import TestWithServers
from command_utils_base import CommandFailure
from general_utils import DaosTestError
from server_utils import DaosServerManager

class CriticalIntegration(TestWithServers):
    """Test Class Description: Verify the basic integration of
                               the server nodes with available
                               framework and amongst themselves.
    :avocado: recursive
    """


    def test_passwdlessssh(self):
        """
        Test Description: Verify passwordless ssh amongst the server
                          server nodes available.
        :avocado: tags=hw,small
        :avocado: tags=installation,criticalintegration,passwdlessssh
        """

        check_remote_root_access = self.params.get("check_remote_root_access", "/run/*")
        daos_server_version_list = []
        dmg_version_list = []
        for host in self.hostlist_servers:
            daos_server_cmd = ("ssh -oNumberOfPasswordPrompts=0 {}"
                              " 'daos_server version'".format(host))
            remote_root_access = ("ssh -oNumberOfPasswordPrompts=0 root@{}"
                                  " 'echo hello'".format(host))
            command_for_inter_node = ("pdsh -S -w {}"
                                      " 'echo hello'".format(','.join(self.hostlist_servers)))
            try:
                out = run_command(daos_server_cmd)
                daos_server_version_list.append(out.stdout.split(b' ')[3])
                if check_remote_root_access:
                    run_command(remote_root_access)
                IorTestBase._execute_command(self, command_for_inter_node, hosts=[host])
            except (DaosTestError, CommandFailure) as error:
                self.fail("Ssh check Failed.\n {}".format(error))

        for host in self.hostlist_clients:
            dmg_version_cmd = ("ssh -oNumberOfPasswordPrompts=0 {}"
                               " 'dmg version -i'".format(host))

            try:
                out = run_command(dmg_version_cmd)
                dmg_version_list.append(out.stdout.split(b' ')[2])
            except DaosTestError as error:
                self.fail("SSH check for client nodes failed.\n {}".format(error))


        result_daos_server = (daos_server_version_list.count(daos_server_version_list[0])
                             == len(daos_server_version_list))
        result_dmg = dmg_version_list.count(dmg_version_list[0]) == len(dmg_version_list)
        result_client_server = daos_server_version_list[0][1:] == dmg_version_list[0]
        
        if (result_daos_server and result_dmg and result_client_server):
            self.log.info("All servers have same daos version")
            self.log.info("All clients have same daos version")
            self.log.info("Servers and Clients have same daos version")
        else:
            self.log.info("Not all servers and clients have same daos version")
            self.log.info("Clients: {}".format(dmg_version_list))
            self.log.info("servers: {}".format(daos_server_version_list))
            self.fail()

    def test_ras(self):
        """
        Test Description: Verify RAS event on all server nodes from testrunner

        :avocado: tags=hw,small
        :avocado: tags=installation,criticalintegration,ras
        """

        dmg = self.get_dmg_command()
        rank_list = self.server_managers[0].get_host_ranks(self.hostlist_servers)
        print("rank_list: {}".format(rank_list))

        for rank in rank_list:
            dmg.system_stop(ranks=rank)
            time.sleep(5)
            dmg.system_start(ranks=rank)
            time.sleep(5)

        dmg.storage_scan()
        dmg.network_scan()
