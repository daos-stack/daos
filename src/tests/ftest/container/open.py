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


class OpenContainerTest(TestWithServers):
    """
    Tests DAOS container bad create (non existing pool handle, bad uuid)
    and close.

    :avocado: recursive
    """

    def test_container_open(self):
        """
        Test basic container bad create.

        :avocado: tags=all,container,tiny,full_regression,containeropen
        """
        self.result_pass = "PASS"
        self.result_fail = "FAIL"
        self.pool = None
        self.container = None

        # common parameters used in pool create
        mode = self.params.get("mode", '/run/createtests/createmode/')
        name = self.params.get("setname", '/run/createtests/createset/')
        size = self.params.get("size", '/run/createtests/createsize/')
        uuidlist = self.params.get("uuid", '/run/createtests/uuids/*/')
        pohlist = self.params.get("poh", '/run/createtests/handles/*/')

        # Find out the overall expected test result
        expected_for_param = []
        expected_for_param.append(uuidlist[1])
        expected_for_param.append(pohlist[1])
        expected_result = self.result_pass
        for result in expected_for_param:
            if result == self.result_fail:
                expected_result = self.result_fail
                break

        pool_uuid = uuidlist[0]
        pool_gid = uuidlist[0]
        if uuidlist[1] == self.result_pass:
            # Use system UID if we're testing with good UID
            pool_uuid = os.geteuid()
            pool_gid = os.getegid()

        try:
            # Create and connect to the pool
            self.pool = DaosPool(self.context)
            self.pool.create(mode, pool_uuid, pool_gid, size, name, None)
            self.pool.connect(1 << 1)

            # Define pool handle for container open
            if pohlist[1] == self.result_pass:
                poh = self.pool.handle
            else:
                poh = pohlist[0]

            # Create a container with the pool
            self.container = DaosContainer(self.context)
            self.container.create(poh)

            # Define test UUID for container open
            struuid = self.container.get_uuid_str()
            container_uuid = uuid.UUID(struuid)

            # Try to open the container
            self.container.open(poh, container_uuid)

            # wait a few seconds and then destroy the container
            time.sleep(5)
            self.destroy_containers(self.container)

            # cleanup the pool
            self.destroy_pools(self.pool)

            if expected_result == self.result_fail:
                self.fail("DAOS API error was expected, but never thrown.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == self.result_pass:
                self.fail("Test was expected to pass but DAOS API error was thrown.\n")


if __name__ == "__main__":
    main()
