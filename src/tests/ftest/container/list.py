#!/usr/bin/python3
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers
from daos_utils import DaosCommand


class ListContainerTest(TestWithServers):
    """
    Test Class Description:
        Tests daos pool list-cont. Test the following cases.
        1. Create 1 container, list, and verify the listed container UUID
            matches the UUID returned when created.
        2. Create 1 more container and verify list; 2 total containers
        3. Create 98 more containers and verify list; 100 total containers
        4. Create 2 additional pools and create 10 containers in each pool.
            Verify the container UUIDs in these 2 new pools.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ListContainerTest object."""
        super().__init__(*args, **kwargs)
        self.daos_cmd = None

    def create_list(self, count, pool_uuid, expected_uuids):
        """Create container and call daos pool list-cont to list and verify.

        Args:
            count (Integer): Number of containers to create.
            pool_uuid (String): Pool UUID to create containers in.
            expected_uuids (List of string): List that contains container UUID.
                It should contain all the container UUIDs for the given pool.
        """
        # Create containers and store the container UUIDs into expected_uuids.
        for _ in range(count):
            expected_uuids.append(
                self.daos_cmd.container_create(pool=pool_uuid)["response"]["container_uuid"])
        expected_uuids.sort()

        # Call container list and collect the UUIDs.
        data = self.daos_cmd.container_list(pool=pool_uuid)
        actual_uuids = []
        for uuid_label in data["response"]:
            actual_uuids.append(uuid_label["uuid"])
        actual_uuids.sort()

        self.assertEqual(expected_uuids, actual_uuids)

    def test_list_containers(self):
        """Jira ID: DAOS-3629

        Test Description:
            Test daos pool list-cont

        Use Cases:
            See test cases in the class description.

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=container,list_containers
        """
        expected_uuids1 = []
        self.pool = []
        self.pool.append(self.get_pool(connect=False))
        self.daos_cmd = DaosCommand(self.bin)

        # 1. Create 1 container and list.
        self.create_list(1, self.pool[0].uuid, expected_uuids1)

        # 2. Create 1 more container and list; 2 total.
        self.create_list(1, self.pool[0].uuid, expected_uuids1)

        # 3. Create 98 more containers and list; 100 total.
        self.create_list(98, self.pool[0].uuid, expected_uuids1)

        # 4. Create 2 additional pools and create 10 containers in each pool.
        self.pool.append(self.get_pool(connect=False))
        self.pool.append(self.get_pool(connect=False))

        # Create 10 containers in pool 2 and verify.
        expected_uuids2 = []
        self.create_list(10, self.pool[1].uuid, expected_uuids2)

        # Create 10 containers in pool 3 and verify.
        expected_uuids3 = []
        self.create_list(10, self.pool[2].uuid, expected_uuids3)
