#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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
from __future__ import print_function

import time
import traceback
import uuid
import os

from avocado import main
from apricot import TestWithServers

from daos_api import DaosPool, DaosContainer, DaosApiError

def create_pool_and_connect(pool_mode, pool_size, pool_name, pool_context):
    """
    Create a pool, call its create function, and return the created pool
    """
        new_pool = DaosPool(pool_context)
        new_pool.create(pool_mode, os.geteuid(), os.getegid(), pool_size,
            pool_name, None)
        new_pool.connect(1 << 1)
        return new_pool

class OpenContainerTest(TestWithServers):
    """
    Tests container's open function. Create 2 pool-container pairs and test
    the following.
    1. Use valid pool handle and container UUID. Expected to pass
    container1.open(pool1.handle, container1_uuid)
    container2.open(pool2.handle, container2_uuid)
    2. Use the other container's UUID. Expected to fail
    container1.open(pool1.handle, container2_uuid)
    container2.open(pool2.handle, container1_uuid)
    3. Use the other pool's handle. Expected to fail
    container1.open(pool2.handle, container1_uuid)
    container2.open(pool1.handle, container2_uuid)
    4. Use the other container's UUID and the other pool's handle.
    Expected to fail
    container1.open(pool2.handle, container2_uuid)
    container2.open(pool1.handle, container1_uuid)

    :avocado: recursive
    """

    def test_container_open(self):
        """
        Test container's open function as described above

        :avocado: tags=all,container,tiny,full_regression,containeropen
        """
        result_pass = "PASS"
        result_fail = "FAIL"

        # Get parameters from open.yaml
        mode = self.params.get("mode", '/run/createtests/createmode/')
        name = self.params.get("setname", '/run/createtests/createset/')
        size = self.params.get("size", '/run/createtests/createsize/')
        uuid_states = self.params.get("uuid",
            '/run/createtests/container_uuid_states/*/')
        poh_states = self.params.get("poh", '/run/createtests/handle_states/*/')

        try:
            # Prepare 2 pools and 2 containers. Also prepare the 2 container
            # UUIDs
            pool1 = create_pool_and_connect(mode, size, name, self.context)
            pool2 = create_pool_and_connect(mode, size, name, self.context)
            container1 = DaosContainer(self.context)
            container1.create(pool1.handle)
            str_uuid = container1.get_uuid_str()
            container1_uuid = uuid.UUID(str_uuid)
            container2 = DaosContainer(self.context)
            container2.create(pool2.handle)
            str_uuid = container2.get_uuid_str()
            container2_uuid = uuid.UUID(str_uuid)
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Error while creating pool or container")

        if uuid_states[0] == result_pass and poh_states[0] == result_pass:
            # Test 1: Use valid pool handle and container UUID
            try:
                # Case 1
                container1.open(pool1.handle, container1_uuid)
                # Case 2
                container2.open(pool2.handle, container2_uuid)
            except DaosApiError as excep:
                print(excep)
                print(traceback.format_exc())
                self.fail("Error while opening the container with valid " +
                    "pool handle and container UUID")
        elif uuid_states[0] == result_pass and poh_states[0] == result_fail:
            # Test 2: Use the other container's UUID
            try:
                # Case 1
                container1.open(pool1.handle, container2_uuid)
                self.fail("No error occurred from using container 2's UUID " +
                    "while opening container 1")
            except DaosApiError as excep:
                print(excep)
                print(traceback.format_exc())
            try:
                # Case 2
                container2.open(pool2.handle, container1_uuid)
                self.fail("No error occurred from using container 1's UUID " +
                    "while opening container 2")
            except DaosApiError as excep:
                print(excep)
                print(traceback.format_exc())
        elif uuid_states[0] == result_fail and poh_states[0] == result_pass:
            # Test 3: Use the other pool's handle
            try:
                # Case 1
                container1.open(pool2.handle, container1_uuid)
                self.fail("No error occurred from using pool 2's handle " +
                    "while opening container 1")
            except DaosApiError as excep:
                print(excep)
                print(traceback.format_exc())
            try:
                # Case 2
                container2.open(pool1.handle, container2_uuid)
                self.fail("No error occurred from using pool1's handle " +
                    "while opening container 2")
            except DaosApiError as excep:
                print(excep)
                print(traceback.format_exc())
        else:
            # Test 4: Use the other container's UUID and the other pool's handle
            try:
                # Case 1
                container1.open(pool2.handle, container2_uuid)
                self.fail("No error occurred from using pool 2's handle " +
                    "and container 2's UUID while opening container 1")
            except DaosApiError as excep:
                print(excep)
                print(traceback.format_exc())
            try:
                # Case 2
                container2.open(pool1.handle, container1_uuid)
                self.fail("No error occurred from using pool1's handle " +
                    "and container 1's UUID while opening container 2")
            except DaosApiError as excep:
                print(excep)
                print(traceback.format_exc())

if __name__ == "__main__":
    main()
