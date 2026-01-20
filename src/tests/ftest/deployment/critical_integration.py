"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import json

# Imports need to be split or python fails to import
from apricot import TestWithoutServers, TestWithServers
from ClusterShell.NodeSet import NodeSet
from exception_utils import CommandFailure
from general_utils import DaosTestError, get_journalctl, journalctl_time, run_command
from run_utils import run_remote

# pylint: disable-next=fixme
# TODO Provision all daos nodes using provisioning tool provided by HPCM


class CriticalIntegrationWithoutServers(TestWithoutServers):
    """Test Class Description: Verify the basic integration of
                               the server nodes with available
                               framework and amongst themselves.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CriticalIntegrationWithoutServers object."""
        super().__init__(*args, **kwargs)
        self.hostlist_servers = NodeSet()
        self.hostlist_clients = NodeSet()

    def setUp(self):
        """Set up CriticalIntegrationWithoutServers."""
        super().setUp()
        self.hostlist_servers = self.get_hosts_from_yaml(
            "test_servers", "server_partition", "server_reservation", "/run/hosts/*")
        self.hostlist_clients = self.get_hosts_from_yaml(
            "test_clients", "server_partition", "server_reservation", "/run/hosts/*")

    def test_passwdlessssh_versioncheck(self):
        # pylint: disable=protected-access
        """
        Test Description: Verify password-less ssh amongst the server
                          server nodes available and verify all server
                          and client nodes have same daos versions.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,critical_integration
        :avocado: tags=CriticalIntegrationWithoutServers,test_passwdlessssh_versioncheck
        """
        check_remote_root_access = self.params.get("check_remote_root_access", "/run/*")
        libfabric_path = self.params.get("libfabric_path", "/run/*")
        daos_server_version_list = []
        dmg_version_list = []
        failed_nodes = NodeSet()
        for host in self.hostlist_servers:
            daos_server_cmd = ("ssh -oNumberOfPasswordPrompts=0 {}"
                               " 'daos_server version -j'".format(host))
            remote_root_access = ("ssh -oNumberOfPasswordPrompts=0 root@{}"
                                  " 'echo hello'".format(host))
            command_for_inter_node = ("clush --nostdin -S -b -w {}"
                                      " 'echo hello'".format(str(self.hostlist_servers)))
            try:
                out = json.loads((run_command(daos_server_cmd)).stdout)
                daos_server_version_list.append(out['response']['version'])
                if check_remote_root_access:
                    run_command(remote_root_access)
                if not run_remote(self.log, NodeSet(host), command_for_inter_node).passed:
                    self.fail(f"Inter-node clush failed on {host}")
            except (DaosTestError, CommandFailure, KeyError) as error:
                self.log.error("Error: %s", error)
                failed_nodes.add(host)
        if failed_nodes:
            self.fail("SSH check failed on the following nodes.\n {}".format(failed_nodes))

        for host in self.hostlist_clients:
            dmg_version_cmd = ("ssh -oNumberOfPasswordPrompts=0 {}"
                               " dmg version -i -j".format(host))

            try:
                out = json.loads((run_command(dmg_version_cmd)).stdout)
                dmg_version_list.append(out['response']['version'])
            except (DaosTestError, KeyError) as error:
                self.log.error("Error: %s", error)
                failed_nodes.add(host)
        if failed_nodes:
            self.fail("SSH check for client nodes failed.\n {}".format(failed_nodes))

        result_daos_server = (daos_server_version_list.count(daos_server_version_list[0])
                              == len(daos_server_version_list))
        result_dmg = dmg_version_list.count(dmg_version_list[0]) == len(dmg_version_list)
        result_client_server = daos_server_version_list[0] == dmg_version_list[0]

        # libfabric version check
        # pylint: disable-next=unsupported-binary-operation
        all_nodes = self.hostlist_servers | self.hostlist_clients
        libfabric_version_cmd = "clush -S -b -w {} {}/fi_info --version".format(
            all_nodes, libfabric_path)
        libfabric_output = run_command(libfabric_version_cmd)
        if len(all_nodes) == 1:
            same_libfab_nodes = 1
        else:
            same_libfab_nodes = libfabric_output.stdout_text.split('\n')[1].split('(')[1][:-1]
        libfabric_version = libfabric_output.stdout_text.split('\n')[3].split(' ')[1]
        result_libfabric_version = int(same_libfab_nodes) == len(all_nodes)

        if (result_daos_server and result_dmg and result_client_server
                and result_libfabric_version):
            self.log.info("All servers have same daos version")
            self.log.info("All clients have same daos version")
            self.log.info("Servers and Clients have same daos version")
            self.log.info("Servers and Clients have same libfabric version")
            self.log.info("Clients: %s", dmg_version_list)
            self.log.info("servers: %s", daos_server_version_list)
            self.log.info("Libfabric Version on Servers and Clients: %s", libfabric_version)
        else:
            self.log.info("Not all servers and clients have either same daos version or \
                          libfabric version")
            self.log.info("Clients: %s", dmg_version_list)
            self.log.info("servers: %s", daos_server_version_list)
            self.log.info("Libfabric Version Output: %s", libfabric_output.stdout)
            self.fail()


class CriticalIntegrationWithServers(TestWithServers):
    """Test Class Description: Verify the basic integration of
                               the server nodes with available
                               framework and amongst themselves.
    :avocado: recursive
    """

    def test_ras(self):
        """
        Test Description: Verify RAS event on all server nodes from testrunner.
                          Verify network scan and storage scan for server nodes.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=deployment,critical_integration,control
        :avocado: tags=CriticalIntegrationWithServers,test_ras
        """
        dmg = self.get_dmg_command()
        rank_list = self.server_managers[0].get_host_ranks(self.hostlist_servers)
        self.log.info("rank_list: %s", rank_list)
        half_num_ranks = len(rank_list) // 2
        # divide total ranks list into two halves to save time during system stop
        sub_rank_list = [rank_list[:half_num_ranks], rank_list[half_num_ranks:]]
        self.log.info("sub_rank_list: %s", sub_rank_list)

        # stop ranks, verify they stopped successfully and restart the stopped ranks
        since = journalctl_time()
        for sub_list in sub_rank_list:
            ranks_to_stop = ",".join([str(rank) for rank in sub_list])
            self.log.info("Ranks to stop: %s", ranks_to_stop)
            # stop ranks and verify if they stopped
            dmg.system_stop(ranks=ranks_to_stop)
            check_stopped_ranks = self.server_managers[0].check_rank_state(sub_list,
                                                                           ["stopped", "excluded"],
                                                                           5)
            if check_stopped_ranks:
                self.log.info("Ranks %s failed to stop", check_stopped_ranks)
                self.fail("Failed to stop ranks cleanly")

            # restart stopped ranks and verify if they are joined
            dmg.system_start(ranks=ranks_to_stop)
            check_started_ranks = self.server_managers[0].check_rank_state(sub_list, ["joined"], 5)
            if check_started_ranks:
                self.fail("Following Ranks {} failed to restart".format(check_started_ranks))

        until = journalctl_time()

        # gather journalctl logs for each server host, verify system stop event was sent to logs
        results = get_journalctl(hosts=self.hostlist_servers, since=since,
                                 until=until, journalctl_type="daos_server")
        str_to_match = "daos_engine exited: signal: killed"
        for count, host in enumerate(self.hostlist_servers):
            occurrence = results[count]["data"].count(str_to_match)
            if occurrence != 2:
                self.log.info("Occurrence %s for rank stop not as expected for host %s",
                              occurrence, host)
                msg = "Rank shut down message not found in journalctl! Output = {}".format(
                    results[count]["data"])
                self.fail(msg)

        dmg.storage_scan()
        dmg.network_scan()
