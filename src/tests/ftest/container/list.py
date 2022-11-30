'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers


class ContainerListTest(TestWithServers):
    """
    Test Class Description:
        Tests daos pool list-cont. Test the following cases.
        1. Create 1 container, list, and verify the listed container UUID and label
            matches the UUID and label returned when created.
        2. Create 1 more container and verify list; 2 total containers
        3. Create 98 more containers and verify list; 100 total containers
        4. Create 2 additional pools and create 10 containers in each pool.
            Verify the container UUIDs and labels in these 2 new pools.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ListContainerTest object."""
        super().__init__(*args, **kwargs)
        self.daos_cmd = None

    def create_list(self, count, pool, expected_uuids, expected_labels):
        """Create container and call daos pool list-cont to list and verify.

        Args:
            count (int): Number of containers to create.
            pool (TestPool): pool create containers in.
            expected_uuids (list of str): List that contains container UUIDs.
                It should contain all the container UUIDs for the given pool.
            expected_labels (list of str): List that contains container labels.
                It should contain all the container labels for the given pool.

        """
        # Create containers and store expected UUIDs and labels
        for _ in range(count):
            cont = self.get_container(pool)
            expected_uuids.append(cont.uuid.lower())
            expected_labels.append(cont.label.value)
        expected_uuids.sort()
        expected_labels.sort()

        # Call container list and collect the UUIDs and labels.
        data = self.daos_cmd.container_list(pool=pool.identifier)
        actual_uuids = []
        actual_labels = []
        for uuid_label in data["response"]:
            actual_uuids.append(uuid_label["uuid"])
            actual_labels.append(uuid_label["label"])
        actual_uuids.sort()
        actual_labels.sort()

        self.assertEqual(expected_uuids, actual_uuids, 'Container UUIDs differ')
        self.assertEqual(expected_labels, actual_labels, 'Container labels differ')

    def test_list_containers(self):
        """Jira ID: DAOS-3629

        Test Description:
            Test daos pool list-cont

        Use Cases:
            See test cases in the class description.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=ContainerListTest,list_containers,test_list_containers
        """
        expected_uuids1 = []
        expected_labels1 = []
        self.pool = []
        self.pool.append(self.get_pool(connect=False))
        self.daos_cmd = self.get_daos_command()

        # 1. Create 1 container and list.
        self.create_list(1, self.pool[0], expected_uuids1, expected_labels1)

        # 2. Create 1 more container and list; 2 total.
        self.create_list(1, self.pool[0], expected_uuids1, expected_labels1)

        # 3. Create 98 more containers and list; 100 total.
        self.create_list(98, self.pool[0], expected_uuids1, expected_labels1)

        # 4. Create 2 additional pools and create 10 containers in each pool.
        self.pool.append(self.get_pool(connect=False))
        self.pool.append(self.get_pool(connect=False))

        # Create 10 containers in pool 2 and verify.
        expected_uuids2 = []
        expected_labels2 = []
        self.create_list(10, self.pool[1], expected_uuids2, expected_labels2)

        # Create 10 containers in pool 3 and verify.
        expected_uuids3 = []
        expected_labels3 = []
        self.create_list(10, self.pool[2], expected_uuids3, expected_labels3)
