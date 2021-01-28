#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback
import uuid

from apricot import TestWithServers
from avocado.core.exceptions import TestFail

RESULT_PASS = "PASS"
RESULT_FAIL = "FAIL"
RESULT_TO_NUM = {RESULT_PASS: 0, RESULT_FAIL: 1}


class OpenContainerTest(TestWithServers):
    """
    Test Class Description:
        Tests container's open function. Create 2 pool-container pairs
        and test the following.
        1. Use valid pool handle and container UUID. Expected to pass
        container1.open(pool1.handle, container1_uuid)
        container2.open(pool2.handle, container2_uuid)
        2. Use the other container's UUID. Expected to fail
        container1.open(pool1.handle, container2_uuid)
        container2.open(pool2.handle, container1_uuid)
        3. Use the other pool's handle. Expected to fail
        container1.open(pool2.handle, container1_uuid)
        container2.open(pool1.handle, container2_uuid)

    :avocado: recursive
    """

    def test_container_open(self):
        """JIRA ID: DAOS-3223

        Test Description:
            Test container's open function as described above

        Use Cases:
            Open container with valid and invalid pool handle and container
            UUID

        :avocado: tags=all,small,container,full_regression,container_open
        """
        self.pool = []
        self.container = []

        uuid_state = self.params.get("uuid", "/run/uuid_poh/*/")
        poh_state = self.params.get("poh", "/run/uuid_poh/*/")

        expected_result = RESULT_PASS
        if uuid_state == RESULT_FAIL or poh_state == RESULT_FAIL:
            expected_result = RESULT_FAIL

        # Derive the test case number from the PASS/FAIL-PASS/FAIL combination
        poh_state_num = RESULT_TO_NUM[poh_state]
        uuid_state_num = RESULT_TO_NUM[uuid_state]
        test_case = (poh_state_num << 1) + uuid_state_num

        # Prepare the messages for the 3 test cases.
        # Test Bug! indicates that there's something wrong with the test since
        # it shouldn't reach that point
        messages_case_1 = [
            "Case 1: Test Bug!",
            "Error while opening the container with valid pool handle and " +
            "container UUID",
            "Case 1: Test Bug!",
            "Error while opening the container with valid pool handle and " +
            "container UUID"
        ]
        messages_case_2 = [
            "No error occurred from using container 2's UUID while opening " +
            "container 1",
            "Case 2: Test Bug!",
            "No error occurred from using container 1's UUID while opening " +
            "container 2",
            "Case 2: Test Bug!"
        ]
        messages_case_3 = [
            "No error occurred from using pool 2's handle while opening " +
            "container 1",
            "Case 3: Test Bug!",
            "No error occurred from using pool1's handle while opening " +
            "container 2",
            "Case 3: Test Bug!"
        ]
        result_messages = [messages_case_1, messages_case_2, messages_case_3]

        # Create the pool and connect. Then create a container with the pool.
        # Add the pool and the container created into the list.
        container_uuids = []
        for _ in range(2):
            self.pool.append(self.get_pool())
            self.container.append(self.get_container(pool=self.pool[-1]))
            container_uuids.append(uuid.UUID(self.container[-1].uuid))

        # Decide which pool handle and container UUID to use. The PASS/FAIL
        # number corresponds to the index for self.pool and container_uuids
        pool_handle_index = poh_state_num
        container_uuid_index = uuid_state_num

        # Case 1
        try:
            self.container[0].open(
                self.pool[pool_handle_index].pool.handle,
                container_uuids[container_uuid_index])
            self.assertEqual(
                expected_result, RESULT_PASS, result_messages[test_case][0])
        except TestFail as excep:
            print(excep)
            print(traceback.format_exc())
            self.assertEqual(
                expected_result, RESULT_FAIL, result_messages[test_case][1])

        # Case 2. Symmetric to Case 1. Use the other handle and UUID
        pool_handle_index = pool_handle_index ^ 1
        container_uuid_index = container_uuid_index ^ 1
        try:
            self.container[1].open(
                self.pool[pool_handle_index].pool.handle,
                container_uuids[container_uuid_index])
            self.assertEqual(
                expected_result, RESULT_PASS, result_messages[test_case][2])
        except TestFail as excep:
            print(excep)
            print(traceback.format_exc())
            self.assertEqual(
                expected_result, RESULT_FAIL, result_messages[test_case][3])
