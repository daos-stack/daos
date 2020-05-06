#!/usr/bin/python
'''
  (C) Copyright 2018-2020 Intel Corporation.

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
'''
import traceback
import uuid

from apricot import TestWithServers
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from avocado.core.exceptions import TestFail
from pydaos.raw import c_uuid

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
        """Jira ID: DAOS-3223

        Test Description:
            Test container's open function as described above

        Use Cases:
            Open container with valid and invalid pool handle and container
            UUID

        :avocado: tags=all,container,tiny,full_regression,container_open
        """
        self.pool = []
        self.container = []

        # Get parameters from open.yaml
        poh_states = self.params.get("poh", '/run/handle_states/*/')
        uuid_states = self.params.get("uuid", '/run/container_uuid_states/*/')

        # Derive the test case number from the PASS/FAIL-PASS/FAIL combination
        poh_state_num = RESULT_TO_NUM[poh_states[0]]
        uuid_state_num = RESULT_TO_NUM[uuid_states[0]]
        test_case = (poh_state_num << 1) + uuid_state_num
        if test_case == 3:
            # Passing in handle2 and UUID2 to open will overwrite the
            # container's members, so container1 effectively becomes container2
            # and that wouldn't be a good test, so skip
            self.cancel("Skip the case with container1.open(pool_handle2, " +
                        "container_uuid2)")

        expected_for_param = []
        expected_for_param.append(uuid_states[0])
        expected_for_param.append(poh_states[0])
        expected_result = RESULT_PASS
        for result in expected_for_param:
            if result == RESULT_FAIL:
                expected_result = RESULT_FAIL
                break

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

        # Create the pool and connect. Then create the container from the pool
        # Add the pool and the container created into a list
        container_uuids = []
        for _ in range(2):
            self.pool.append(TestPool(
                self.context, dmg_command=self.get_dmg_command()))
            self.pool[-1].get_params(self)
            self.pool[-1].create()
            self.pool[-1].connect()
            self.container.append(TestContainer(pool=self.pool[-1]))
            self.container[-1].get_params(self)
            self.container[-1].create()
            container_uuids.append(uuid.UUID(self.container[-1].uuid))
            #container_cuuids.append(self.container[-1].container.uuid)

        # Decide which pool handle and container UUID to use. The PASS/FAIL
        # number corresponds to the index for self.pool and container_uuids
        pool_handle_index = poh_state_num
        container_uuid_index = uuid_state_num

        # Case 1
        try:
            self.container[0].open(
                self.pool[pool_handle_index].pool.handle.value,
                container_uuids[container_uuid_index])
            self.assertEqual(
                expected_result, RESULT_PASS, result_messages[test_case][0])
        except TestFail as excep:
            print(excep)
            print(traceback.format_exc())
            self.assertEqual(
                expected_result, RESULT_FAIL, result_messages[test_case][1])
            # Calling container.open with invalid pool handle or UUID results
            # in setting them in the underlying DaosContainer's member. When we
            # try to destroy it with API in tearDown, it'll pass in the invalid
            # handle/UUID member to the API and causes an error. Reset them
            # back to the original values.
            self.container[0].container.poh = \
                self.pool[0].pool.handle.value
            # For some reason, directly setting uuid causes an error during
            # destroy, so use c_uuid as in DaosContainer class.
            c_uuid(container_uuids[0], self.container[0].container.uuid)

        # Case 2. Symmetric to Case 1. Use the other handle and UUID
        pool_handle_index = pool_handle_index ^ 1
        container_uuid_index = container_uuid_index ^ 1
        try:
            self.container[1].open(
                self.pool[pool_handle_index].pool.handle.value,
                container_uuids[container_uuid_index])
            self.assertEqual(
                expected_result, RESULT_PASS, result_messages[test_case][2])
        except TestFail as excep:
            print(excep)
            print(traceback.format_exc())
            self.assertEqual(
                expected_result, RESULT_FAIL, result_messages[test_case][3])
            self.container[1].container.poh = \
                self.pool[1].pool.handle.value
            c_uuid(container_uuids[1], self.container[1].container.uuid)
