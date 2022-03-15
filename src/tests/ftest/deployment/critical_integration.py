#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from general_utils import run_command, DaosTestError
from ior_test_base import IorTestBase
from apricot import TestWithServers
from exception_utils import CommandFailure


class CriticalIntegration(TestWithServers):
    """Test Class Description: Verify the basic integration of
                               the server nodes with available
                               framework and amongst themselves.
    :avocado: recursive
    """

    #TO-DO
    #Provision all daos nodes using provisioning tool provided by HPCM

    def test_passwdlessssh_versioncheck(self):
        # pylint: disable=protected-access
        """
        Test Description: Verify passwordless ssh amongst the server
                          server nodes available and verify all server
                          and client nodes have same daos versions.
        :avocado: tags=all,deployment,full_regression
        :avocado: tags=hw,large
        :avocado: tags=criticalintegration,passwdlessssh_versioncheck
        """

        check_remote_root_access = self.params.get("check_remote_root_access", "/run/*")
        daos_server_version_list = []
        dmg_version_list = []
        failed_nodes = []
        for host in self.hostlist_servers:
            daos_server_cmd = ("ssh -oNumberOfPasswordPrompts=0 {}"
                               " 'daos_server version'".format(host))
            remote_root_access = ("ssh -oNumberOfPasswordPrompts=0 root@{}"
                                  " 'echo hello'".format(host))
            command_for_inter_node = ("clush --nostdin -S -w {}"
                                      " 'echo hello'".format(','.join(self.hostlist_servers)))
            try:
                out = run_command(daos_server_cmd)
                daos_server_version_list.append(out.stdout.split(b' ')[3])
                if check_remote_root_access:
                    run_command(remote_root_access)
                IorTestBase._execute_command(self, command_for_inter_node, hosts=[host])
            except (DaosTestError, CommandFailure) as error:
                self.log.error("Error: %s", error)
                failed_nodes.append(host)
        if failed_nodes:
            self.fail("SSH check failed on the following nodes.\n {}".format(failed_nodes))

        for host in self.hostlist_clients:
            dmg_version_cmd = ("ssh -oNumberOfPasswordPrompts=0 {}"
                               " 'dmg version -i'".format(host))

            try:
                out = run_command(dmg_version_cmd)
                dmg_version_list.append(out.stdout.split(b' ')[2])
            except DaosTestError as error:
                self.log.error("Error: %s", error)
                failed_nodes.append(host)
        if failed_nodes:
            self.fail("SSH check for client nodes failed.\n {}".format(failed_nodes))

        result_daos_server = (daos_server_version_list.count(daos_server_version_list[0])
                              == len(daos_server_version_list))
        result_dmg = dmg_version_list.count(dmg_version_list[0]) == len(dmg_version_list)
        result_client_server = daos_server_version_list[0][1:] == dmg_version_list[0]

        if (result_daos_server and result_dmg and result_client_server):
            self.log.info("All servers have same daos version")
            self.log.info("All clients have same daos version")
            self.log.info("Servers and Clients have same daos version")
            self.log.info("Clients: %s", dmg_version_list)
            self.log.info("servers: %s", daos_server_version_list)
        else:
            self.log.info("Not all servers and clients have same daos version")
            self.log.info("Clients: %s", dmg_version_list)
            self.log.info("servers: %s", daos_server_version_list)
            self.fail()

    def test_ras(self):
        """
        Test Description: Verify RAS event on all server nodes from testrunner.
                          Verify network scan and storage scan for server nodes.
        :avocado: tags=all,deployment,full_regression
        :avocado: tags=hw,large
        :avocado: tags=criticalintegration,ras
        """

        dmg = self.get_dmg_command()
        rank_list = self.server_managers[0].get_host_ranks(self.hostlist_servers)
        self.log.info("rank_list: %s", rank_list)
        for rank in rank_list:
            dmg.system_stop(ranks=rank)
            if self.server_managers[0].check_rank_state(rank, "stopped", 5):
                dmg.system_start(ranks=rank)
                if not self.server_managers[0].check_rank_state(rank, "joined", 5):
                    self.fail("Rank {} failed to restart".format(rank))
            else:
                self.fail("Rank {} failed to stop".format(rank))
        dmg.storage_scan()
        dmg.network_scan()
