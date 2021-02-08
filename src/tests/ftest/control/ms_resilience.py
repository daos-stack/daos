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
        self.L_QUERY_TIMER = 15

    def update_and_verify(self, ignore_status=False):
        """Create a pool on the server group

        Args:
            ignore_status (bool): If False, raise an exception when pool create
                fails, otherwise don't raise an exception. Defaults to False.
        """
        self.add_pool(create=False)
        self.pool.name.value = self.server_group
        try:
            self.pool.create()
        except TestFail as error:
            if ignore_status:
                self.log.info("Expected MS error creating pool!: %s", error)
            else:
                self.fail("Pool create failed unexpectedly!")

        if not ignore_status:
            self.log.info("Pool UUID %s on server group: %s",
                          self.pool.uuid, self.server_group)
            # Verify that the pool persisted.
            if self.get_dmg_command().pool_list()["response"]["pools"]:
                self.log.info("Found pool in system.")
            else:
                self.fail("No pool found in system.")

    def verify_leader(self, access_list):
        """Verify the leader of the MS is in the access_list.

        Args:
            access_list (list): list of hostname representing the access points
                for the MS.

        Returns:
            str: hostname of the MS leader. If the leader is not within the
                access_list, the test will fail.

        """
        l_addr = None
        start = time.time()
        while not l_addr and (time.time() - start) < self.L_QUERY_TIMER:
            sys_leader_info = self.get_dmg_command().system_leader_query()
            l_addr = sys_leader_info["response"]["CurrentLeader"]
            time.sleep(1)

        if not l_addr:
            self.fail("Timer exceeded for verifying leader. No leader found!")

        l_hostname, _, _ = socket.gethostbyaddr(l_addr.split(":")[0])
        l_hostname = l_hostname.split(".")[0]

        # Check that the new leader is in the access list
        if l_hostname not in access_list:
            self.fail("Selected leader <{}> is not within the access_list"
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
        access_list = random.sample(self.hostlist_servers, resilience_num)
        server_groups = {
            self.server_group:
                {
                    "hosts": self.hostlist_servers,
                    "access_points": access_list
                },
        }
        self.start_servers(server_groups)
        return access_list

    def verify_resiliency(self, N):
        """Verify 2N+1 resiliency up to N = 2

        This method will launch 2 * N + 1 servers, stop the leader of MS,
        make sure to remove leader from access_list, verify that a new leader
        has been elected from the remaining hosts in access_list and verify
        that MS is stll accessible by creating a new pool.
        """
        access_list = self.launch_servers((2 * N) + 1)
        leader = self.verify_leader(access_list)
        stop_processes([leader], "daos_server")

        access_list = [x for x in access_list if x != leader]
        self.get_dmg_command().hostlist = access_list[0]

        self.verify_leader(access_list)

        # ignore_status should be set to False at some point when issue with
        # pool ranks is resolved on MS service.
        self.update_and_verify(ignore_status=False)
        self.pool = None

    def verify_lost_resiliency(self, N):
        """Test that even with 2N+1 resiliency lost, reads still work.

        This method will launch 2 * N + 1 servers, will use a kill_list to
        shutdown N + 1 access_list hosts, including the MS leader. Stopped
        hosts will be removed from the access_list and then verify that we can
        access MS to read by performing a dmg system command. It then tries to
        unsuccessfully make an update to MS. Lastly, we bring back the killed
        servers and check that MS is once again available for writing.
        """
        access_list = self.launch_servers((2 * N) + 1)
        leader = self.verify_leader(access_list)

        kill_list = set(random.sample(access_list, N))
        kill_list.add(leader)
        stop_processes(kill_list, "daos_server")

        access_list = [x for x in access_list if x not in kill_list]
        self.get_dmg_command().hostlist = access_list[0]

        if not self.get_dmg_command().system_leader_query():
            self.fail("Can't read MS information after removing resiliency.")

        self.update_and_verify(ignore_status=True)
        self.start_additional_servers(additional_servers=list(kill_list))
        self.update_and_verify(ignore_status=False)
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

        :avocado: tags=all,hw,large,full_regression,control,ms_resilience
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

        :avocado: tags=all,hw,large,full_regression,control,ms_resilience
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
        This test case will shutdown 1 + 1 access_list hosts and check we've
        lost resiliency.

        :avocado: tags=all,hw,large,full_regression,control,ms_resilience
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
        This test case will shutdown 2 + 1 access_list hosts and check we've
        lost resiliency.

        :avocado: tags=all,hw,large,full_regression,control,ms_resilience
        :avocado: tags=ms_lost_resilience_N_2
        """
        # Run test case
        self.verify_lost_resiliency(2)
