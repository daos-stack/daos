#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

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
        super(ListContainerTest, self).__init__(*args, **kwargs)
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
        kwargs = {"pool": pool_uuid}
        for _ in range(count):
            expected_uuids.append(
                self.daos_cmd.get_output("container_create", **kwargs)[0])
        expected_uuids.sort()
        # daos pool list-cont returns container UUIDs as a list.
        data = self.daos_cmd.pool_list_cont(**kwargs)
        actual_uuids = data["uuids"]
        actual_uuids.sort()
        self.assertEqual(expected_uuids, actual_uuids)

    def test_list_container(self):
        """Jira ID: DAOS-3629

        Test Description:
            Test daos pool list-cont

        Use Cases:
            See test cases in the class description.

        :avocado: tags=all,container,full_regression,list_containers
        """
        expected_uuids1 = []
        data1 = self.get_dmg_command().pool_create(scm_size="150MB")
        self.daos_cmd = DaosCommand(self.bin)

        # 1. Create 1 container and list.
        self.create_list(1, data1["uuid"], expected_uuids1)

        # 2. Create 1 more container and list; 2 total.
        self.create_list(1, data1["uuid"], expected_uuids1)

        # 3. Create 98 more containers and list; 100 total.
        self.create_list(98, data1["uuid"], expected_uuids1)

        # 4. Create 2 additional pools and create 10 containers in each pool.
        data2 = self.get_dmg_command().pool_create(scm_size="150MB")
        data3 = self.get_dmg_command().pool_create(scm_size="150MB")

        # Create 10 containers in pool 2 and verify.
        expected_uuids2 = []
        self.create_list(10, data2["uuid"], expected_uuids2)
        # Create 10 containers in pool 3 and verify.
        expected_uuids3 = []
        self.create_list(10, data3["uuid"], expected_uuids3)
