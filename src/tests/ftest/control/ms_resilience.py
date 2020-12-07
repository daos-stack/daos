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

from apricot import TestWithServers
from general_utils import stop_processes
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
        self.dmg = self.get_dmg_command()

    def test_ms_resilience(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible after instances are removed.

        :avocado: tags=all,full_regression,large,control,ms_resilience
        """
        # Start up 2 servers as access_points, so MS will be replicated
        access_list = random.sample(self.hostlist_servers, 3)
        server_groups = {
            self.server_group:
                {"servers": self.hostlist_servers, "access_list": access_list},
        }
        self.start_servers(server_groups)

        # Create a pool on the group
        self.add_pool(create=False)
        self.pool.name.value = self.server_group
        self.pool.create()
        self.log.info("Pool UUID %s on server group: %s",
                      self.pool.uuid, self.server_group)

        # Get leader of the MS
        l1_addr = self.dmg.system_leader_query()["leader"]
        l1_hostname, _, _ = socket.gethostbyaddr(l1_addr.split(":")[0])

        # Check that the new leader is in the access list
        if l1_hostname not in access_list:
            self.fail("Selected leader <{}> is not within the access_list"
                      " provied to servers".format(l1_hostname))

        # Shutdown the server that is the leader in the MS
        stop_processes(l1_hostname, "daos_server")

        # Verify that the MS is still accessible
        # Remove the server that was stopped from the access_list
        access_list = [x for x in access_list if x != l1_hostname.split(".")[0]]
        self.dmg.hostlist = access_list[0]

        # Give it a sec or two for a new leader to be selected.
        time.sleep(2)
        l2_addr = self.dmg.system_leader_query()["leader"]
        l2_hostname, _, _ = socket.gethostbyaddr(l2_addr.split(":")[0])

        # Check that the new leader is in the access list
        if l2_hostname not in access_list:
            self.fail("Selected leader <{}> is not within the access_list"
                      " provied to servers".format(l2_hostname))

        # Verify that the pool persisted.
        pool_data = self.dmg.pool_list()
        if pool_data and self.pool.uuid in pool_data:
            self.log.info("Found pool in system.")
        else:
            self.fail("No pool found in system.")
