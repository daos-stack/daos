"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import socket
import time

from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers


class ManagementServiceFailover(TestWithServers):
    """Test Class Description:
    Verify that MS leader resigns on hard dRPC failure and that a new
    leader steps up to service the request.

    :avocado: recursive
    """

    L_QUERY_TIMER = 30

    def __init__(self, *args, **kwargs):
        """Initialize a ManagementServiceFailover object."""
        super().__init__(*args, **kwargs)
        self.setup_start_servers = False
        self.start_servers_once = False

    def get_leader(self):
        """Fetch the current system leader.

        Returns:
            str: hostname of the MS leader, or None
        """
        sys_leader_info = self.get_dmg_command().system_leader_query()
        l_addr = sys_leader_info["response"]["current_leader"]

        if not l_addr:
            return None

        l_hostname, _, _ = socket.gethostbyaddr(l_addr.split(":")[0])
        l_hostname = l_hostname.split(".")[0]

        return l_hostname

    def verify_leader(self, replicas):
        """Verify the leader of the MS is in the replicas.

        Args:
            replicas (list): list of hostnames representing the access points
                for the MS.

        Returns:
            str: address of the MS leader.

        """
        l_hostname = None
        start = time.time()
        while not l_hostname and (time.time() - start) < self.L_QUERY_TIMER:
            l_hostname = self.get_leader()
            # Check that the new leader is in the access list
            if l_hostname not in replicas:
                self.log.error("Selected leader <%s> is not within the replicas"
                               " provided to servers", l_hostname)
            time.sleep(1)

        elapsed = time.time() - start
        if not l_hostname:
            self.fail("No leader found after {:.2f}s!".format(elapsed))

        self.log.info("*** found leader (%s) after %.2fs", l_hostname, elapsed)
        return l_hostname

    def launch_servers(self, replica_count=5):
        """Setup and start the daos_servers.

        Args:
            replica_count (int): Number of replicas to launch.

        Returns:
            list: list of access point hosts where MS has been started.

        """
        self.log.info("*** launching %d servers", replica_count)
        replicas = NodeSet.fromlist(self.random.sample(list(self.hostlist_servers), replica_count))
        server_groups = {
            self.server_group:
                {
                    "hosts": self.hostlist_servers,
                    "access_points": replicas,
                    "svr_config_file": None,
                    "dmg_config_file": None,
                    "svr_config_temp": None,
                    "dmg_config_temp": None
                },
        }
        self.start_servers(server_groups)
        return replicas

    def kill_server_ranks(self, server):
        """Kill the ranks on a server in order to simulate failures.

        Args:
          server (str): hostname of server hosting ranks.

        Returns:
          kill_ranks: list of ranks that were killed.

        """
        kill_ranks = self.server_managers[0].get_host_ranks(server)
        self.log.info("*** killing ranks %s on %s", kill_ranks, server)
        self.server_managers[0].stop_ranks(kill_ranks, self.d_log, force=True)

        self.server_managers[0].update_expected_states(
            kill_ranks, ["stopped", "excluded"])

        return kill_ranks

    def test_ms_failover(self):
        """
        JIRA ID: DAOS-7124

        Test Description:
            Test that the MS leader resigns on dRPC failure and that a new
            leader is elected.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control
        :avocado: tags=ManagementServiceFailover,test_ms_failover
        """
        replicas = self.launch_servers()
        leader = self.verify_leader(replicas)

        # First, kill the ranks on the leader so that it can't service
        # requests that need dRPC.
        self.kill_server_ranks(leader)

        # Next, attempt to create a pool. The request should eventually
        # succeed.
        self.get_pool(connect=False)

        # Finally, verify that one of the replicas has stepped up as
        # the new leader.
        survivors = [x for x in replicas if x is not leader]
        self.verify_leader(survivors)
        self.get_dmg_command().hostlist = self.hostlist_servers

        # Dump the current system state.
        self.get_dmg_command().system_query()
