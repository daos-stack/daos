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
from apricot import TestWithServers
from check_for_pool import check_for_pool


class DynamicServerPool(TestWithServers):
    """Test Class Description:
    Test to create pools on single, multiple, and dynamically added server.

    This test covers the following requirements.
    SRS-10-0054: New server can be used for new pool.
    SRS-10-0105: A pool spanning only a subset of the servers can be created
    starting from 1 server up to all the servers. Query should be used to verify
    that the pool was allocated on the requested servers.

    First, we'll create one pool on the first server and another pool over two
    servers. This covers SRS-10-0105.

    Second, we start a new server and let it join the existing two servers.
    Then we'll create one pool on this new server and another one over all three
    servers. This covers SRS-10-0054.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DynamicServerPool object."""
        super(DynamicServerPool, self).__init__(*args, **kwargs)
        self.expected_uuids = []

    def setUp(self):
        super(DynamicServerPool, self).setUp()
        self.pool = []

    def verify_uuids(self):
        """Call dmg pool list to get the list of UUIDs and verify against the
        expected UUIDs.
        """
        actual_uuids = []
        output_list = self.get_dmg_command().get_output("pool_list")
        for uuid_sr in output_list:
            actual_uuids.append(uuid_sr[0])
        actual_uuids.sort()
        self.expected_uuids.sort()
        self.assertEqual(self.expected_uuids, actual_uuids)

    def check_pool_location(self, hosts, uuid_to_ranks):
        """Iterate pools and verify that the UUID-named directory exists in
        each hosts.

        Args:
            hosts (list of str): Hostnames to search the UUID-named directory.
            uuid_to_ranks (str to list of int dictionary): UUID to rank list
                dictionary.
        """
        RC_SUCCESS = 0
        for pool in self.pool:
            # Note that we don't check mapping between rank and hostname, but it
            # appears that self.hostlist_servers[0] is always rank0, 1 is rank1,
            # and the extra server we'll be adding will be rank2.
            for rank, host in enumerate(hosts):
                rc = check_for_pool(host, pool.uuid.lower())
                pool_exists_on_host = rc == RC_SUCCESS
                # If this rank is in the rank list, there should be the
                # UUID-named directory; i.e., pool_exist_on_host is True.
                self.assertEqual(
                    rank in uuid_to_ranks[pool.uuid.lower()],
                    pool_exists_on_host)

    def test_dynamic_server_pool(self):
        """
        JIRA ID: DAOS-3595

        Test Description: See class description.

        :avocado: tags=all,medium,control,full_regression,dynamic_server_pool
        """
        uuid_to_ranks = {}

        # Create a pool on rank0.
        self.pool.append(self.get_pool(create=False))
        self.pool[-1].target_list.update([0], "pool.target_list")
        self.pool[-1].create()
        self.expected_uuids.append(self.pool[-1].uuid.lower())
        uuid_to_ranks[self.pool[-1].uuid.lower()] = [0]
        # Create a pool across the 2 servers.
        self.pool.append(self.get_pool())
        self.pool[-1].create()
        self.expected_uuids.append(self.pool[-1].uuid.lower())
        uuid_to_ranks[self.pool[-1].uuid.lower()] = [0, 1]
        # Verify UUIDs by calling dmg pool list.
        self.verify_uuids()

        # Verify that the UUID-named directory is created, or not created, at
        # each host for the two pools.
        self.check_pool_location(self.hostlist_servers, uuid_to_ranks)

        # Start an additional server.
        extra_servers = self.params.get("test_servers", "/run/extra_servers/*")
        self.log.info("Extra Servers = %s", extra_servers)
        self.start_additional_servers(extra_servers)

        # Create a pool on the newly added server and verify the UUIDs with dmg
        # pool list.
        self.pool.append(self.get_pool(create=False))
        self.pool[-1].target_list.update([2], "pool.target_list")
        self.pool[-1].create()
        self.expected_uuids.append(self.pool[-1].uuid.lower())
        uuid_to_ranks[self.pool[-1].uuid.lower()] = [2]
        self.verify_uuids()
        # Verify that the UUID-named directory is created at each host including
        # the new host.
        self.check_pool_location(
            self.hostlist_servers + extra_servers, uuid_to_ranks)

        # Create a new pool across all three servers and verify the UUIDs with
        # dmg pool list.
        self.pool.append(self.get_pool())
        self.pool[-1].create()
        self.expected_uuids.append(self.pool[-1].uuid.lower())
        uuid_to_ranks[self.pool[-1].uuid.lower()] = [0, 1, 2]
        self.verify_uuids()
        # Verify that the UUID-named directory is created at each host for all
        # pools.
        self.check_pool_location(
            self.hostlist_servers + extra_servers, uuid_to_ranks)
