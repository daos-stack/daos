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

from avocado import main
from apricot import TestWithServers

from pydaos.raw import DaosPool, DaosContainer, DaosApiError


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
        self.pool = []
        self.container = []

        # common parameters used in pool create
        mode = self.params.get("mode", '/run/createtests/createmode/')
        name = self.params.get("setname", '/run/createtests/createset/')
        size = self.params.get("size", '/run/createtests/createsize/')

        # pool UIDs & GIDs
        uid = [
            self.params.get("uid", "/run/createtests/createuid1/"),
            self.params.get("uid", "/run/createtests/createuid2/"),
        ]
        gid = [
            self.params.get("gid", "/run/createtests/creategid1/"),
            self.params.get("gid", "/run/createtests/creategid2/"),
        ]

        container_uuid = None
        expected_for_param = []
        uuidlist = self.params.get("uuid", '/run/createtests/uuids/*/')
        container_uuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        pohlist = self.params.get("poh", '/run/createtests/handles/*/')
        poh = pohlist[0]
        expected_for_param.append(pohlist[1])

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        try:
            # Create and connect to two pools
            for index in range(2):
                self.pool.append(DaosPool(self.context))
                self.pool[index].create(
                    mode, uid[index], gid[index], size, name, None)
                self.pool[index].connect(1 << 1)

            # defines pool handle for container open
            if pohlist[0] == 'pool1':
                poh = self.pool[0].handle
            else:
                poh = self.pool[1].handle

            # Create a container in the first pool
            self.container.append(DaosContainer(self.context))
            self.container[0].create(self.pool[0].handle)

            # defines test UUID for container open
            if uuidlist[0] == 'pool1':
                struuid = self.container[0].get_uuid_str()
                container_uuid = uuid.UUID(struuid)
            else:
                container_uuid = uuid.uuid4()   # random uuid

            # Try to open the first container
            # open should be ok only if poh = pool1.handle &&
            #                           containerUUID = container1.uuid
            self.container[0].open(poh, container_uuid)

            # wait a few seconds and then destroy containers
            time.sleep(5)
            self.destroy_containers(self.container)

            # cleanup the pools
            self.destroy_pools(self.pool)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")


if __name__ == "__main__":
    main()
