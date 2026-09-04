'''
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import queue
import threading

from apricot import TestWithServers


class ListContainerTest(TestWithServers):
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
    def __container_create(self, pool, result_queue):
        """
        Create a container in the given pool and put the result in the result queue.

        Args:
            pool (TestPool): The pool in which to create the container.
            result_queue (queue.Queue): Queue to store the result of the container creation.
        """
        try:
            container = self.get_container(pool, create=False)
            result = container.create()
            result_queue.put(
                (result["response"]["container_uuid"], result["response"]["container_label"]))
        except Exception as error:  # pylint: disable=broad-except
            result_queue.put(error)

    def create_list(self, count, pool, expected_uuids_labels):
        """Create container and call daos pool list-cont to list and verify.

        Args:
            count (int): Number of containers to create.
            pool (TestPool): pool to create containers in.
            expected_uuids_labels (list): list of tuples containing expected (uuid, label) for
                all containers in the pool
        """
        result_queue = queue.Queue()

        # Create containers in parallel and store the container UUIDs and labels
        threads = []
        for _ in range(count):
            thread = threading.Thread(target=self.__container_create, args=(pool, result_queue))
            thread.start()
            threads.append(thread)

        for thread in threads:
            thread.join()

        while not result_queue.empty():
            result = result_queue.get()
            if isinstance(result, Exception):
                self.fail(f"Container creation failed with error: {result}")
            expected_uuids_labels.append(result)

        expected_uuids_labels.sort()

        # Call container list and collect the UUIDs.
        data = self.get_daos_command().container_list(pool=pool.identifier)
        actual_uuids_labels = []
        for uuid_label in data["response"]:
            actual_uuids_labels.append((uuid_label["uuid"], uuid_label["label"]))
        actual_uuids_labels.sort()

        self.assertEqual(
            expected_uuids_labels, actual_uuids_labels,
            "Container UUIDs and labels from list do not match those from create")

    def test_list_containers(self):
        """Jira ID: DAOS-3629

        Test Description:
            Test daos pool list-cont

        Use Cases:
            See test cases in the class description.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=ListContainerTest,test_list_containers
        """
        expected_uuids_labels1 = []
        pool = []
        self.log_step('Create the first pool')
        pool.append(self.get_pool(connect=False))

        self.log_step('Create 1 container in the first pool and verify container list')
        self.create_list(1, pool[0], expected_uuids_labels1)

        self.log_step('Create 1 more container in the first pool and verify container list')
        self.create_list(1, pool[0], expected_uuids_labels1)

        self.log_step('Create 98 more containers in the first pool and verify container list')
        self.create_list(98, pool[0], expected_uuids_labels1)

        self.log_step('Create 2 additional pools')
        pool.append(self.get_pool(connect=False))
        pool.append(self.get_pool(connect=False))

        self.log_step('Create 10 containers in the second pool and verify container list')
        expected_uuids_labels2 = []
        self.create_list(10, pool[1], expected_uuids_labels2)

        self.log_step('Create 10 containers in the third pool and verify container list')
        expected_uuids_labels3 = []
        self.create_list(10, pool[2], expected_uuids_labels3)
