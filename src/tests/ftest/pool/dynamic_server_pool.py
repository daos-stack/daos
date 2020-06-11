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
from test_utils_pool import TestPool
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

    def create_new_pool(self, ranks=None):
        """Create a new pool using ranks and append it to self.pool.

        Args:
            ranks (str, optional): Ranks value passed in to --ranks. Defaults to
            None.
        """
        new_pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        new_pool.get_params(self)
        if ranks:
            new_pool.target_list.update(ranks)
        new_pool.create()
        self.expected_uuids.append(new_pool.uuid.lower())
        self.pool.append(new_pool)

    def verify_uuids(self):
        """Call dmg pool list to get the list of UUIDs and verify against the
        expected UUIDs passed in.
        """
        actual_uuids = []
        output_list = self.get_dmg_command().get_output("pool_list")
        for uuid_sr in output_list:
            actual_uuids.append(uuid_sr[0])
        actual_uuids.sort()
        self.expected_uuids.sort()
        self.assertEqual(self.expected_uuids, actual_uuids)

    def test_dynamic_server_pool(self):
        """
        JIRA ID: DAOS-3598

        Test Description: See class description.

        :avocado: tags=all,medium,control,full_regression,dynamic_server_pool
        """
        self.pool = []

        # Create a pool on rank0.
        self.create_new_pool("0")
        # Create a pool across the 2 servers.
        self.create_new_pool()
        # Verify UUIDs by calling dmg pool list.
        self.verify_uuids()

        # Verify that the UUID-named directory is created, or not created, at
        # each host for the two pools.
        # Note that there's no mapping between rank and hostname, but it appears
        # that self.hostlist_servers[0] is always rank0, 1 is rank1, and the
        # extra server we'll be adding will be rank2.
        RC_SUCCESS = 0
        RC_FAIL = 1
        rc_p1h1 = check_for_pool(
            self.hostlist_servers[0], self.pool[0].uuid.lower())
        self.assertEqual(rc_p1h1, RC_SUCCESS)
        rc_p1h2 = check_for_pool(
            self.hostlist_servers[1], self.pool[0].uuid.lower())
        self.assertEqual(rc_p1h2, RC_FAIL)
        rc_p2h1 = check_for_pool(
            self.hostlist_servers[0], self.pool[1].uuid.lower())
        self.assertEqual(rc_p2h1, RC_SUCCESS)
        rc_p2h2 = check_for_pool(
            self.hostlist_servers[1], self.pool[1].uuid.lower())
        self.assertEqual(rc_p2h2, RC_SUCCESS)

        # Start an additional server.
        extra_servers = self.params.get("test_servers", "/run/extra_servers/*")
        self.log.info("Extra Servers = %s", extra_servers)
        self.start_additional_servers(extra_servers)

        # Create a pool on the newly added server and verify the UUIDs with dmg
        # pool list.
        self.create_new_pool("2")
        self.verify_uuids()
        # Verify that the UUID-named directory is created, or not created, at
        # each host for the new pool.
        rc_p3h1 = check_for_pool(
            self.hostlist_servers[0], self.pool[2].uuid.lower())
        self.assertEqual(rc_p3h1, RC_FAIL)
        rc_p3h2 = check_for_pool(
            self.hostlist_servers[1], self.pool[2].uuid.lower())
        self.assertEqual(rc_p3h2, RC_FAIL)
        rc_p3h3 = check_for_pool(extra_servers[0], self.pool[2].uuid.lower())
        self.assertEqual(rc_p3h3, RC_SUCCESS)

        # Create a new pool across all three servers and verify the UUIDs with
        # dmg pool list.
        self.create_new_pool()
        self.verify_uuids()
        # Verify that the UUID-named directory is created, or not created, at
        # each host for the new pool.
        rc_p4h1 = check_for_pool(
            self.hostlist_servers[0], self.pool[3].uuid.lower())
        self.assertEqual(rc_p4h1, RC_SUCCESS)
        rc_p4h2 = check_for_pool(
            self.hostlist_servers[1], self.pool[3].uuid.lower())
        self.assertEqual(rc_p4h2, RC_SUCCESS)
        rc_p4h3 = check_for_pool(extra_servers[0], self.pool[3].uuid.lower())
        self.assertEqual(rc_p4h3, RC_SUCCESS)
