#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from datetime import datetime

from general_utils import run_command, DaosTestError, get_journalctl
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
        half_num_ranks = len(rank_list)//2
        # divide total ranks list into two halves to save time during system stop
        sub_rank_list = [rank_list[x:x+half_num_ranks] for x in range(0, len(rank_list),
                                                                      half_num_ranks)]
        self.log.info("sub_rank_list: %s", sub_rank_list)

        # stop ranks, verify they stopped successfully and restart the stopped ranks
        since = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        for sub_list in sub_rank_list:
            ranks_to_stop = ",".join([str(rank) for rank in sub_list])
            self.log.info("Ranks to stop: {}", ranks_to_stop)
            dmg.system_stop(ranks=ranks_to_stop)
            for rank in sub_list:
                if self.server_managers[0].check_rank_state(rank, "stopped", 5):
                    dmg.system_start(ranks=rank)
                    if not self.server_managers[0].check_rank_state(rank, "joined", 5):
                        self.fail("Rank {} failed to restart".format(rank))
                else:
                    self.fail("Rank {} failed to stop".format(rank))
        until = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        # gather journalctl logs for each server host, verify system stop event was sent to logs
        results = get_journalctl(hosts=self.hostlist_servers, since=since,
                                 until=until, journalctl_type="daos_server")
        for count, _ in enumerate(self.hostlist_servers):
            occurence = results[count]["data"].count("cleaning engine")
            if occurence != 2:
                msg = "Rank shut down message not found in journalctl! Output = {}".format(
                    results)
                self.fail(msg)

        dmg.storage_scan()
        dmg.network_scan()
