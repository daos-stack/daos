#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.

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

from apricot import TestWithServers
from general_utils import stop_processes
from avocado.core.exceptions import TestFail
import random
import socket
import time


class ManagementServiceResilience(TestWithServers):
    """Test Class Description:
    Verify that MS is accessible as long as one instance is left.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Inititialize a ManagementServiceResilience object."""
        super(ManagementServiceResilience, self).__init__(*args, **kwargs)
        self.setup_start_servers = False
        self.start_servers_once = False
        self.L_QUERY_TIMER = 300 # adjust down as we learn more

    def find_pool(self, uuid):
        """Find a pool in the output of `dmg pool list`.

        Args:
            uuid (str): Pool UUID to find.

        Returns:
            Pool entry, if found, or None.
        """
        pools = self.get_dmg_command().pool_list()["response"]["pools"]
        for pool in pools:
            if pool["uuid"].lower() == uuid.lower():
                return pool
        return None

    def create_pool(self, failure_expected=False):
        """Create a pool on the server group

        Args:
            failure_expected (bool): If False, raise an exception when pool
            create fails, otherwise don't raise an exception. Defaults to False.
        """
        self.add_pool(create=False)
        self.pool.name.value = self.server_group
        self.log.info("*** creating pool (should fail? %s)", failure_expected)
        try:
            self.pool.create()
        except TestFail as error:
            if failure_expected:
                self.log.info("Expected MS error creating pool!: %s", error)
            else:
                self.fail("Pool create failed unexpectedly!")

        if not failure_expected:
            self.log.info("Pool UUID %s on server group: %s",
                          self.pool.uuid, self.server_group)
            # Verify that the pool persisted.
            if self.find_pool(self.pool.uuid):
                self.log.info("Found pool in system.")
            else:
                self.fail("No pool found in system.")

    def verify_leader(self, replicas):
        """Verify the leader of the MS is in the replicas.

        Args:
            replicas (list): list of hostnames representing the access points
                for the MS.

        Returns:
            str: hostname of the MS leader. If the leader is not within the
                replicas, the test will fail.

        """
        l_addr = None
        start = time.time()
        while not l_addr and (time.time() - start) < self.L_QUERY_TIMER:
            sys_leader_info = self.get_dmg_command().system_leader_query()
            l_addr = sys_leader_info["response"]["CurrentLeader"]
            time.sleep(1)

        elapsed = time.time() - start
        if not l_addr:
            self.fail("No leader found after {:.2f}s!".format(elapsed))

        l_hostname, _, _ = socket.gethostbyaddr(l_addr.split(":")[0])
        l_hostname = l_hostname.split(".")[0]
        self.log.info("*** found leader (%s) after %.2fs", l_hostname, elapsed)

        # Check that the new leader is in the access list
        if l_hostname not in replicas:
            self.fail("Selected leader <{}> is not within the replicas"
                      " provided to servers".format(l_hostname))
        return l_hostname

    def launch_servers(self, resilience_num):
        """Setup and start the daos_servers.

        Args:
            resilience_num (int): minimum amount of MS replicas to achieve
                resiliency.

        Returns:
            list: list of access point hosts where MS has been started.

        """
        self.log.info("*** launching %d servers", resilience_num)
        replicas = random.sample(self.hostlist_servers, resilience_num)
        server_groups = {
            self.server_group:
                {
                    "hosts": self.hostlist_servers,
                    "access_points": replicas
                },
        }
        self.start_servers(server_groups)
        return replicas

    def verify_resiliency(self, N):
        """Verify 2N+1 resiliency

        This method will launch 2 * N + 1 servers, stop the MS leader,
        remove the leader host from replicas, verify that a new leader
        has been elected from the remaining hosts in replicas and verify
        that the MS is stll accessible by creating a new pool.
        """
        replicas = self.launch_servers((2 * N) + 1)
        leader = self.verify_leader(replicas)

        # First, kill the leader plus just enough other replicas to
        # push up to the edge of quorum loss.
        kill_list = set(random.sample(replicas, N))
        if leader not in kill_list:
            kill_list.pop()
            kill_list.add(leader)
        self.log.info("*** stopping leader (%s) + %d others", leader, N-1)
        stop_processes(kill_list, "daos_server")

        # Next, verify that one of the replicas has stepped up as
        # the new leader.
        survivors = [x for x in replicas if x not in kill_list]
        self.verify_leader(survivors)
        self.get_dmg_command().hostlist = self.hostlist_servers

        # Finally, verify that quorum has been retained by performing
        # write operations.
        self.create_pool()
        self.pool = None

    def verify_lost_resiliency(self, N):
        """Test that even with 2N+1 resiliency lost, reads still work.

        This method will launch 2 * N + 1 servers, will use a kill_list to
        shutdown N + 1 replica hosts, including the MS leader. Stopped
        hosts will be removed from the replicas and then verify that we can
        access MS to read by performing a dmg system command. It then tries to
        unsuccessfully make an update to MS. Lastly, we bring back the killed
        servers and check that MS is once again available for writing.
        """
        replicas = self.launch_servers((2 * N) + 1)
        leader = self.verify_leader(replicas)

        # First, create a pool.
        self.create_pool(failure_expected=False)

        # Next, kill the leader plus enough other replicas to
        # lose quorum.
        kill_list = set(random.sample(replicas, N+1))
        if leader not in kill_list:
            kill_list.pop()
            kill_list.add(leader)
        self.log.info("*** stopping leader (%s) + %d others", leader, N)
        stop_processes(kill_list, "daos_server")
        self.get_dmg_command().hostlist = self.hostlist_servers

        # Now, try to perform some read-only operations to verify
        # that they work on a MS without quorum.
        if not self.get_dmg_command().system_leader_query():
            self.fail("Can't query system after quorum loss.")
        if not self.find_pool(self.pool.uuid):
            self.fail("Can't list pools after quorum loss.")
        self.pool = None

        # A write operation should fail, however.
        self.create_pool(failure_expected=True)

        # Finally, restart the dead servers and verify that quorum is
        # regained, which should allow for write operations to succeed again.
        self.restart_servers(self.server_group, list(kill_list))
        self.verify_leader(replicas)
        self.create_pool(failure_expected=False)
        self.pool = None

    def test_ms_resilience_1(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible after instances are removed.

        The raft protocol guarantees 2N+1 resiliency, where N is the number of
        nodes that can fail while leaving the cluster in a state where it can
        continue to make progress. i.e. N=1, a minimum of 3 nodes is needed in
        the cluster and it will tolerate 1 node failure.

        N = 1 servers as access_points where, N = failure tolerance,
        resilience_num = minimum amount of MS replicas to achieve resiliency.
        This test case will shutdown 1 server and verify we still have MS
        resiliency.

        :avocado: tags=all,pr,daily_regression,control,ms_resilience
        :avocado: tags=ms_resilience_N_1
        """
        # Run test cases
        self.verify_resiliency(1)

    def test_ms_resilience_2(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible after instances are removed.

        The raft protocol guarantees 2N+1 resiliency, where N is the number of
        nodes that can fail while leaving the cluster in a state where it can
        continue to make progress. i.e. N=1, a minimum of 3 nodes is needed in
        the cluster and it will tolerate 1 node failure.

        N = 2 servers as access_points where, N = failure tolerance,
        resilience_num = minimum amount of MS replicas to achieve resiliency.
        This test case will shutdown 1 server and verify we still have MS
        resiliency.

        :avocado: tags=all,pr,daily_regression,control,ms_resilience
        :avocado: tags=ms_resilience_N_2
        """
        # Run test cases
        self.verify_resiliency(2)

    def test_ms_resilience_3(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible for reading with resiliency
            lost.

        The raft protocol guarantees 2N+1 resiliency, where N is the number of
        nodes that can fail while leaving the cluster in a state where it can
        continue to make progress. i.e. N=1, a minimum of 3 nodes is needed in
        the cluster and it will tolerate 1 node failure.

        N = 1 servers as access_points where, N = failure tolerance,
        resilience_num = minimum amount of MS replicas to achieve resiliency.
        This test case will shutdown 1 + 1 replicas hosts and check we've
        lost resiliency.

        :avocado: tags=all,pr,daily_regression,control,ms_resilience
        :avocado: tags=ms_lost_resilience_N_1
        """
        # Run test case
        self.verify_lost_resiliency(1)

    def test_ms_resilience_4(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible for reading with resiliency
            lost.

        The raft protocol guarantees 2N+1 resiliency, where N is the number of
        nodes that can fail while leaving the cluster in a state where it can
        continue to make progress. i.e. N=1, a minimum of 3 nodes is needed in
        the cluster and it will tolerate 1 node failure.

        N = 2 servers as access_points where, N = failure tolerance,
        resilience_num = minimum amount of MS replicas to achieve resiliency.
        This test case will shutdown 2 + 1 replicas hosts and check we've
        lost resiliency.

        :avocado: tags=all,pr,daily_regression,control,ms_resilience
        :avocado: tags=ms_lost_resilience_N_2
        """
        # Run test case
        self.verify_lost_resiliency(2)
